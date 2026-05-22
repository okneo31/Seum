# [시스템 개발 지시서] 핵심 여신(대출) 및 코어 뱅킹 트랜잭션 시스템 구축
**문서 분류:** 여신 시스템 아키텍처 및 기능 명세서 (PRD)  
**대상 시스템:** 코어 뱅킹 원장(Ledger) 및 대출 엔진 모듈  

---

## 1. 프로젝트 개요 (Overview)
본 프로젝트는 현대 상업은행의 '신용 창조(Credit Creation)' 및 '만기 변환(Maturity Transformation)' 메커니즘을 데이터베이스 트랜잭션 수준에서 무결성 있게 구현하는 코어 뱅킹 여신 시스템 구축을 목표로 합니다. 대출 실행 시 단순 현금 이동이 아닌, 복식부기 원리(T-Account)에 기반한 원장 확장 및 엄격한 동시성 제어가 본 시스템의 핵심입니다.

---

## 2. 핵심 비즈니스 로직 및 수리적 요구사항

### 2.1. 원리금균등분할상환(Amortization) 계산 엔진
* **요구사항:** 차주가 신청한 대출 원금($P$), 연 이자율($R$, 월 이자율 $r = R / 12 / 100$), 상환 기간($n$, 개월 수)을 입력받아 매월 균등하게 납부할 원리금($PMT$)을 산출하는 알고리즘을 구현한다.
* **수학적 산식:**
    $$PMT = P \times \frac{r(1+r)^n}{(1+r)^n - 1}$$
* **개발 주의사항:** * 부동 소수점 연산 오류(Floating-point drift)를 방지하기 위해, 모든 금리 및 금액 연산 시 반드시 `Decimal` 혹은 고정소수점 라이브러리를 사용한다. (언어별 구현: Python `decimal`, JS `big.js` 등)
    * 매월 발생하는 이자는 `남은 대출 원금 × 월 이자율`로 계산하며, `PMT - 당월 이자`만큼을 원금 상환에 배정하는 동적 스케줄링 테이블을 생성해야 한다.

### 2.2. 법정지급준비율 검증 모듈 (Liquidity & Reserve Check)
* **요구사항:** 대출 실행(신용 창조) 전, 은행 시스템 전체의 가용 본원통화 및 중앙은행 예치금 잔액을 검증한다.
* **로직:** 총 예금 부채액 대비 법정지급준비율($r_{req}$, 예: 7%)을 상회하는 유동성을 확보하고 있는지 확인하여, 한도 초과 시 대출 실행 트랜잭션을 차단하고 에러 코드(`ERR_INSUFFICIENT_RESERVE`)를 반환한다.

---

## 3. 데이터베이스 아키텍처 및 트랜잭션 설계

### 3.1. 데이터 모델링 (원장 테이블 구조 예시)
모든 대출 트랜잭션은 단일 계좌의 잔액 변경이 아닌, 자산(Asset)과 부채(Liability)가 동시에 증가하는 복식부기 형태로 기록되어야 합니다.

#### [Table: `accounts` (계좌 원장)]
| Column Name | Type | Description |
| :--- | :--- | :--- |
| `account_id` | VARCHAR(36) / UUID | PK |
| `user_id` | VARCHAR(36) | 사용자 식별자 |
| `account_type` | ENUM | `DEPOSIT` (부채), `LOAN_RECEIVABLE` (자산) |
| `balance` | DECIMAL(18, 4) | 현재 잔액 (원장 가치) |

#### [Table: `ledger_transactions` (복식부기 트랜잭션 로그)]
| Column Name | Type | Description |
| :--- | :--- | :--- |
| `tx_id` | VARCHAR(36) / UUID | PK |
| `debit_account_id` | VARCHAR(36) | 차변 계좌 ID (자산 증가 / 부채 감소) |
| `credit_account_id` | VARCHAR(36) | 대변 계좌 ID (부채 증가 / 자산 감소) |
| `amount` | DECIMAL(18, 4) | 트랜잭션 금액 |
| `created_at` | TIMESTAMP | 거래 일시 |

### 3.2. 대출 실행 시 트랜잭션 격리 수준 (Isolation Level)
* **격리 수준 설정:** 본 대출 트랜잭션은 금융 데이터의 무결성을 위해 최소 `REPEATABLE READ` 이상을 보장해야 하며, 동시 대출 신청 시 Race Condition 방지를 위해 **비관적 락(Pessimistic Locking)** 또는 `SERIALIZABLE` 격리 수준을 채택한다.
* **SQL 의사코드 (Transaction Block):**
    ```sql
    START TRANSACTION;

    -- 1. 차주의 대출 한도 및 은행의 지급준비율 만족 여부 검증 (Locking)
    SELECT balance FROM accounts WHERE account_type = 'DEPOSIT' FOR UPDATE;

    -- 2. 자산 증가: 대출 채권 자산 계정 생성/업데이트
    INSERT INTO accounts (account_id, user_id, account_type, balance) 
    VALUES ('LOAN_UUID', 'USER_UUID', 'LOAN_RECEIVABLE', 100000000);

    -- 3. 부채 증가 (신용 창조): 차주의 예금 계좌로 대출 금액 입금
    UPDATE accounts 
    SET balance = balance + 100000000 
    WHERE account_id = 'USER_DEPOSIT_UUID';

    -- 4. 복식부기 원장 로그 기입 (차변: 대출채권 증가, 대변: 예금부채 증가)
    INSERT INTO ledger_transactions (tx_id, debit_account_id, credit_account_id, amount)
    VALUES ('TX_UUID', 'LOAN_UUID', 'USER_DEPOSIT_UUID', 100000000);

    COMMIT;
    ```

---

## 4. 핵심 API 명세 (API Specifications)

### 4.1. 대출 상환 스케줄 시뮬레이션 API
* **Endpoint:** `POST /api/v1/loans/simulate`
* **Payload (JSON):**
    ```json
    {
      "principal": 100000000,
      "annual_rate": 4.5,
      "term_months": 24
    }
    ```
* **Response (JSON):**
    ```json
    {
      "monthly_payment": 4364779,
      "schedule": [
        {
          "month": 1,
          "payment": 4364779,
          "principal_paid": 3989779,
          "interest_paid": 375000,
          "remaining_principal": 96010221
        }
        // ... 24개월 반복
      ]
    }
    ```

### 4.2. 대출 실행 (신용 창조) API
* **Endpoint:** `POST /api/v1/loans/originate`
* **Payload (JSON):**
    ```json
    {
      "user_id": "USER_UUID",
      "requested_amount": 100000000,
      "loan_product_id": "PROD_UUID"
    }
    ```
* **Response (JSON):**
    ```json
    {
      "status": "SUCCESS",
      "tx_id": "TX_UUID",
      "loan_account_id": "LOAN_UUID",
      "deposit_account_id": "USER_DEPOSIT_UUID",
      "credited_amount": 100000000,
      "timestamp": "2026-05-23T02:41:00Z"
    }
    ```

---

## 5. 시스템 안정성 및 유동성 리스크 제어 요구사항

1.  **동시성 제어 (Concurrency Control):** 동일 사용자가 다중 디바이스 또는 분산 서버 분할 환경에서 동시에 대출 실행 API를 호출(Double-Spending / Double-Origination 공격)하는 것을 방지하기 위해, `user_id` 단위로 **Redis 분산 락(Distributed Lock)**을 획득한 후 DB 트랜잭션을 시작하도록 설계한다.
2.  **뱅크런 예방 모니터션 및 서킷 브레이커:** 단기 부채(요구불예금)의 급격한 유출 비율을 실시간 센싱한다. 1시간 이내에 당해 은행 총 예금 자산의 5% 이상이 인출되는 이상 징후가 발생할 경우, 신규 대출 집행 API 및 자산 만기 변환 스케줄을 일시 동결하는 **'여신 서킷 브레이커'** 트리거 로직을 구현한다.
3.  **멱등성(Idempotency) 보장:** 모든 대출 실행 요청 헤더에는 UUID 기반의 `X-Idempotency-Key`를 필수 적용하여, 네트워크 재시도 등으로 인한 중복 대출 생성을 원천 차단한다.
