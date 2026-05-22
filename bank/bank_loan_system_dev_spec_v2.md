# [시스템 개발 지시서] 핵심 여신(대출) 및 코어 뱅킹 트랜잭션 시스템 구축 (정수 연산 기반 아키텍처)
**문서 분류:** 여신 시스템 아키텍처 및 기능 명세서 (PRD)  
**대상 시스템:** 코어 뱅킹 원장(Ledger) 및 대출 엔진 모듈  

---

## 1. 프로젝트 개요 (Overview)
본 프로젝트는 현대 상업은행의 '신용 창조(Credit Creation)' 메커니즘을 데이터베이스 트랜잭션 수준에서 무결성 있게 구현하는 코어 뱅킹 여신 시스템 구축을 목표로 합니다. 특히, 금융 데이터의 치명적인 부동소수점 오류(Floating-point drift)를 원천 차단하기 위해 **모든 금액과 비율 데이터를 정수(Integer)로 스케일링**하여 처리하는 아키텍처를 채택합니다.

---

## 2. 핵심 비즈니스 로직 및 정수 연산 원칙

### 2.1. 정수 기반 원리금균등분할상환 계산 엔진
* **정수 연산(Integer Math) 원칙:** 
    * `Decimal` 라이브러리의 오버헤드를 줄이고 연산 정확도를 100% 보장하기 위해, DB 저장 및 백엔드 연산 시 소수점을 사용하지 않는다.
    * **금액:** 원화(KRW)는 1원 단위 정수로, 외화(예: USD)는 센트(Cent) 단위 등 최하위 단위를 기준(1 USD = 100)으로 스케일링한 `BIGINT`로 처리한다.
    * **이자율:** 백분율(%) 대신 **BP(Basis Point, 1bp = 0.01%)** 단위 정수를 사용한다. (예: 연 4.5% = 450bp).
* **수학적 산식 (정수 스케일링 적용):**
    차주 대출 원금($P$), 월 이자율 $r$ (연 이자율 BP / 12 / 10000), 상환 개월 수($n$)
    $$PMT = P 	imes rac{r(1+r)^n}{(1+r)^n - 1}$$
    *(주의: 이자 계산 시 발생하는 소수점 이하는 은행의 '단수 처리 규정'에 따라 절사(Floor) 또는 반올림(Round)하여 완벽한 정수로 맞춘 뒤 원금 상환액에 반영한다.)*

### 2.2. 법정지급준비율 검증 모듈 (Liquidity & Reserve Check)
* **로직:** 총 예금 부채액 대비 법정지급준비율(예: 7% -> 700bp)을 상회하는 유동성을 확보하고 있는지 확인한다. 비율 계산 시 분모/분자에 배수를 곱해 정수 비교로 한도 초과 여부를 판단(`ERR_INSUFFICIENT_RESERVE`)한다.

---

## 3. 데이터베이스 아키텍처 및 트랜잭션 설계

### 3.1. 데이터 모델링 (원장 테이블 구조 예시)
모든 잔액 및 거래 대금은 `DECIMAL`이 아닌 **`BIGINT`** 타입으로 엄격하게 관리된다.

#### [Table: `accounts` (계좌 원장)]
| Column Name | Type | Description |
| :--- | :--- | :--- |
| `account_id` | VARCHAR(36) | PK (UUID) |
| `user_id` | VARCHAR(36) | 사용자 식별자 |
| `account_type` | ENUM | `DEPOSIT` (부채), `LOAN_RECEIVABLE` (자산) |
| `balance` | BIGINT | 현재 잔액 (원화 기준 1원 단위 정수) |

#### [Table: `ledger_transactions` (복식부기 트랜잭션 로그)]
| Column Name | Type | Description |
| :--- | :--- | :--- |
| `tx_id` | VARCHAR(36) | PK (UUID) |
| `debit_account_id` | VARCHAR(36) | 차변 계좌 ID (자산 증가 / 부채 감소) |
| `credit_account_id` | VARCHAR(36) | 대변 계좌 ID (부채 증가 / 자산 감소) |
| `amount` | BIGINT | 트랜잭션 금액 (정수) |
| `created_at` | TIMESTAMP | 거래 일시 |

### 3.2. 대출 실행 시 트랜잭션 격리 수준 (Isolation Level)
* **SQL 의사코드 (Transaction Block):**
    ```sql
    START TRANSACTION;

    -- 1. 지급준비율 만족 여부 검증용 락킹 (Pessimistic Locking)
    SELECT balance FROM accounts WHERE account_type = 'DEPOSIT' FOR UPDATE;

    -- 2. 자산 증가: 대출 채권 자산 생성 (BIGINT)
    INSERT INTO accounts (account_id, user_id, account_type, balance) 
    VALUES ('LOAN_UUID', 'USER_UUID', 'LOAN_RECEIVABLE', 100000000);

    -- 3. 부채 증가: 예금 계좌에 대출액 입금 (BIGINT)
    UPDATE accounts 
    SET balance = balance + 100000000 
    WHERE account_id = 'USER_DEPOSIT_UUID';

    -- 4. 복식부기 원장 기입
    INSERT INTO ledger_transactions (tx_id, debit_account_id, credit_account_id, amount)
    VALUES ('TX_UUID', 'LOAN_UUID', 'USER_DEPOSIT_UUID', 100000000);

    COMMIT;
    ```

---

## 4. 핵심 API 명세 (API Specifications)

### 4.1. 대출 상환 스케줄 시뮬레이션 API
* 클라이언트는 이자율을 BP 단위의 정수로 전송하며, 서버 응답 역시 소수점 없는 정수 금액만 반환합니다. 표시용 변환(소수점 포매팅)은 클라이언트(UI/UX) 영역에서 위임합니다.
* **Endpoint:** `POST /api/v1/loans/simulate`
* **Payload (JSON):**
    ```json
    {
      "principal": 100000000,
      "annual_rate_bps": 450, 
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
      ]
    }
    ```

---

## 5. 시스템 안정성 및 유동성 제어 요구사항
1. **동시성 제어:** 다중 요청에 의한 Double-Origination을 막기 위해 `user_id` 기준 Redis 분산 락을 획득 후 트랜잭션에 진입한다.
2. **단수(짜투리) 정산 규칙:** 매월 이자 계산 시 발생하는 1원 미만의 단수는 절사하여 은행이 부담하거나, 마지막 달 원금 상환 시 일괄 정산하는 로직을 반드시 스케줄링 테이블에 명시한다.
