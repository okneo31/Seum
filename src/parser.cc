#include "parser.h"

#include "error_reporter.h"
#include "utf8.h"

#include <charconv>
#include <string>

namespace seum {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    Program run() {
        Program prog;
        while (peek().kind != TokenKind::EndOfFile) {
            // 계약 선언형 블록 — 결정 87. 메서드+레코드로 desugar 하므로
            // 한 블록이 여러 statement 를 prog 에 직접 append.
            if (check_keyword(U"계약")) {
                parse_contract(prog);
            } else {
                prog.statements.push_back(parse_statement());
            }
        }
        return prog;
    }

private:
    const Token& peek(std::size_t k = 0) const {
        std::size_t idx = i_ + k;
        if (idx >= tokens_.size()) return tokens_.back();
        return tokens_[idx];
    }

    const Token& advance() { return tokens_[i_++]; }

    bool check_keyword(std::u32string_view kw) const {
        return peek().kind == TokenKind::Keyword && peek().text == kw;
    }

    const Token& expect(TokenKind k, const char* expected_utf8) {
        if (peek().kind != k) {
            std::string msg = "기대한 토큰: ";
            msg += expected_utf8;
            msg += "  (실제: ";
            msg += token_kind_name(peek().kind);
            msg += ")";
            raise(peek().pos, msg);
        }
        return advance();
    }

    void expect_keyword(std::u32string_view kw, const char* utf8) {
        if (!check_keyword(kw)) {
            std::string msg = "기대한 키워드: ";
            msg += utf8;
            raise(peek().pos, msg);
        }
        advance();
    }

    void expect_period() {
        if (peek().kind != TokenKind::Period) {
            raise(peek().pos, "문장 끝에 '.' 이 필요합니다");
        }
        advance();
    }

    // === Statements ===
    Stmt parse_statement() {
        if (check_keyword(U"가져오기"))   return parse_import();
        if (check_keyword(U"변수"))       return parse_var_decl();
        if (check_keyword(U"통화"))       return parse_currency_decl();
        if (check_keyword(U"만약"))       return parse_if();
        if (check_keyword(U"반복"))       return parse_repeat();
        if (check_keyword(U"동안"))       return parse_while();
        if (check_keyword(U"함수")) {
            Stmt s = parse_func_decl(/*is_getter=*/false);
            if (!as_func_decl(s)->accumulate_target.empty()) {
                raise(as_func_decl(s)->pos, "'받아서'는 계약 메서드에서만 쓸 수 있습니다");
            }
            return s;
        }
        if (check_keyword(U"지연값"))     return parse_func_decl(/*is_getter=*/true);
        if (check_keyword(U"돌려주기"))   return parse_return();
        return parse_expr_stmt();
    }

    Stmt parse_import() {
        Position pos = peek().pos;
        advance();  // 가져오기
        expect(TokenKind::LParen, "(");
        const Token& name = expect(TokenKind::Identifier, "그릇 이름");
        expect(TokenKind::RParen, ")");
        expect_period();
        return ImportStmt{name.text, pos};
    }

    Stmt parse_var_decl() {
        Position pos = peek().pos;
        advance();  // 변수
        const Token& name = expect(TokenKind::Identifier, "변수 이름");
        expect(TokenKind::Equals, "=");
        Expr value = parse_expression();
        expect_period();
        return VarDeclStmt{name.text, std::move(value), pos};
    }

    Stmt parse_expr_stmt() {
        Position pos = peek().pos;
        Expr e = parse_expression();
        // 필드 대입 — 결정 #81. `대상.필드 = 식.` / `+= 식.` / `-= 식.`
        if (peek().kind == TokenKind::Equals
            || peek().kind == TokenKind::PlusEq
            || peek().kind == TokenKind::MinusEq) {
            std::u32string op = U"=";
            if (peek().kind == TokenKind::PlusEq)  op = U"+=";
            if (peek().kind == TokenKind::MinusEq) op = U"-=";
            Position op_pos = peek().pos;
            advance();  // = / +=
            if (!is_member(e)) {
                raise(op_pos,
                      U"대입 대상은 레코드/계약 필드여야 합니다 (변수 재대입은 '변수'로)");
            }
            Expr value = parse_expression();
            expect_period();
            AssignStmt a;
            a.target = std::move(e);
            a.op     = std::move(op);
            a.value  = std::move(value);
            a.pos    = pos;
            return Stmt{std::move(a)};
        }
        expect_period();
        return ExprStmt{std::move(e), pos};
    }

    // 만약 (조건) 이면 { … } 아니면 { … } — 결정 #28.
    // else-if 는 `아니면 만약 (…) 이면 { … }` 의 nested 형태로 동작.
    Stmt parse_if() {
        Position pos = peek().pos;
        advance();  // 만약
        expect(TokenKind::LParen, "(");
        Expr cond = parse_expression();
        expect(TokenKind::RParen, ")");
        expect_keyword(U"이면", "이면");
        auto then_body = parse_block();
        std::vector<Stmt> else_body;
        if (check_keyword(U"아니면")) {
            advance();  // 아니면
            // 아니면 직후 만약 이 오면 else-if. 그렇지 않으면 일반 블록.
            if (check_keyword(U"만약")) {
                else_body.push_back(parse_if());
            } else {
                else_body = parse_block();
            }
        }
        auto node = std::make_unique<IfStmt>();
        node->cond      = std::move(cond);
        node->then_body = std::move(then_body);
        node->else_body = std::move(else_body);
        node->pos       = pos;
        return Stmt{std::move(node)};
    }

    // 반복 N번 { … } — 결정 #29.
    Stmt parse_repeat() {
        Position pos = peek().pos;
        advance();  // 반복
        Expr count = parse_expression();
        expect_keyword(U"번", "번");
        auto body = parse_block();
        auto node = std::make_unique<RepeatStmt>();
        node->count = std::move(count);
        node->body  = std::move(body);
        node->pos   = pos;
        return Stmt{std::move(node)};
    }

    // 동안 (cond) { … } — 결정 #29.
    Stmt parse_while() {
        Position pos = peek().pos;
        advance();  // 동안
        expect(TokenKind::LParen, "(");
        Expr cond = parse_expression();
        expect(TokenKind::RParen, ")");
        auto body = parse_block();
        auto node = std::make_unique<WhileStmt>();
        node->cond = std::move(cond);
        node->body = std::move(body);
        node->pos  = pos;
        return Stmt{std::move(node)};
    }

    // v0.4a-5b: 타입 매개변수 `이름: 타입` — 타입 주석은 파싱하되 a-5 에선 무시
    // (자산 += 의 통화 검사가 실제 타입 안전성 담당). 결정 §5.
    void skip_param_type() {
        if (peek().kind == TokenKind::Colon) {
            advance();  // :
            expect(TokenKind::Identifier, "매개변수 타입 (BTC/KRW 등)");
        }
    }

    // 함수 이름(p1, p2) -> 결과 { body } — 결정 #30.
    // 지연값 이름() -> 결과 { body } — 결정 #59.
    // 함수 이름(받은돈: BTC) 받아서 -> 대상 { body } — 결정 #65 (계약 메서드).
    Stmt parse_func_decl(bool is_getter) {
        Position pos = peek().pos;
        advance();  // 함수 / 지연값
        const Token& name = expect(TokenKind::Identifier, is_getter ? "지연값 이름" : "함수 이름");
        expect(TokenKind::LParen, "(");
        std::vector<std::u32string> params;
        if (peek().kind != TokenKind::RParen) {
            params.push_back(expect(TokenKind::Identifier, "매개변수 이름").text);
            skip_param_type();
            while (peek().kind == TokenKind::Comma) {
                advance();
                params.push_back(expect(TokenKind::Identifier, "매개변수 이름").text);
                skip_param_type();
            }
        }
        expect(TokenKind::RParen, ")");

        std::u32string accumulate_target;
        std::u32string result_name;
        if (check_keyword(U"받아서")) {
            // 받아서 -> 대상 — 결정 #65. 결과명 대신 누적 대상.
            if (is_getter) {
                raise(peek().pos, "지연값은 '받아서'를 가질 수 없습니다");
            }
            advance();  // 받아서
            expect(TokenKind::Arrow, "->");
            accumulate_target = expect(TokenKind::Identifier, "받아서 누적 대상").text;
            result_name = U"없음";   // 받아서 함수는 없음 반환
        } else {
            if (is_getter && !params.empty()) {
                raise(pos, "지연값은 매개변수를 가질 수 없습니다");
            }
            expect(TokenKind::Arrow, "->");
            if (peek().kind == TokenKind::Identifier) {
                result_name = advance().text;
            } else if (check_keyword(U"없음")) {
                result_name = U"없음";
                advance();
            } else {
                raise(peek().pos, "결과 이름 또는 '없음' 이 와야 합니다");
            }
        }
        if (!accumulate_target.empty() && params.empty()) {
            raise(pos, "'받아서' 함수는 매개변수가 1개 이상 필요합니다");
        }
        auto body = parse_block();
        auto node = std::make_unique<FuncDeclStmt>();
        node->name              = name.text;
        node->params            = std::move(params);
        node->result_name       = std::move(result_name);
        node->body              = std::move(body);
        node->is_getter         = is_getter;
        node->accumulate_target = std::move(accumulate_target);
        node->pos               = pos;
        return Stmt{std::move(node)};
    }

    // 돌려주기 [식].
    Stmt parse_return() {
        Position pos = peek().pos;
        advance();  // 돌려주기
        auto node = std::make_unique<ReturnStmt>();
        node->pos       = pos;
        node->has_value = false;
        if (peek().kind != TokenKind::Period) {
            node->value     = parse_expression();
            node->has_value = true;
        }
        expect_period();
        return Stmt{std::move(node)};
    }

    // 계약 선언형 블록 — 결정 63·87·90.
    // `계약 이름 { 필드 / 함수 }` 를 desugar (백엔드 변경 0):
    //   - 각 메서드(함수) → 전역 FuncDeclStmt (결정 90: 멤버 전역 등록)
    //   - 계약 이름 → 전역 레코드 (필드 + 메서드값). 결정 63: 싱글톤.
    // 블록 내부 항목은 종결자 '.' 없음 (결정 88) — 식 문법이 자기-구분.
    void parse_contract(Program& prog) {
        Position pos = peek().pos;
        advance();  // 계약
        const Token& name = expect(TokenKind::Identifier, "계약 이름");
        expect(TokenKind::LBrace, "{");

        auto record = std::make_unique<RecordExpr>();
        record->pos = pos;

        auto check_dup = [&](const std::u32string& k, Position kp) {
            for (const RecordField& f : record->fields) {
                if (f.key == k) {
                    std::u32string msg = U"계약에 중복된 항목: "; msg += k;
                    raise(kp, msg);
                }
            }
        };

        while (peek().kind != TokenKind::RBrace
            && peek().kind != TokenKind::EndOfFile) {
            if (check_keyword(U"함수") || check_keyword(U"지연값")) {
                bool getter = check_keyword(U"지연값");
                Stmt method = parse_func_decl(getter);
                FuncDeclStmt* fd =
                    std::get<std::unique_ptr<FuncDeclStmt>>(method).get();
                check_dup(fd->name, pos);
                // 받아서 -> 대상 desugar — 결정 65. 본문 앞에
                //   `<계약>.<대상> += <첫 매개변수>.` 주입 (누적 먼저).
                if (!fd->accumulate_target.empty()) {
                    auto cid = std::make_unique<IdentifierExpr>();
                    cid->name = name.text; cid->pos = pos;
                    auto mem = std::make_unique<MemberExpr>();
                    mem->target = Expr{std::move(cid)};
                    mem->field  = fd->accumulate_target;
                    mem->pos    = pos;
                    auto pid = std::make_unique<IdentifierExpr>();
                    pid->name = fd->params[0]; pid->pos = pos;
                    AssignStmt acc;
                    acc.target = Expr{std::move(mem)};
                    acc.op     = U"+=";
                    acc.value  = Expr{std::move(pid)};
                    acc.pos    = pos;
                    fd->body.insert(fd->body.begin(), Stmt{std::move(acc)});
                }
                std::u32string mname = fd->name;
                prog.statements.push_back(std::move(method));   // 메서드 = 전역
                auto id = std::make_unique<IdentifierExpr>();
                id->name = mname;
                id->pos  = pos;
                record->fields.push_back(RecordField{mname, Expr{std::move(id)}});
            } else if (check_keyword(U"자산")) {
                // 자산 이름: 통화 — 결정 64. 통화(0) 으로 초기화 (예: 금고: BTC(0)).
                advance();  // 자산
                const Token& aname = expect(TokenKind::Identifier, "자산 이름");
                check_dup(aname.text, aname.pos);
                expect(TokenKind::Colon, ":");
                const Token& cur = expect(TokenKind::Identifier, "자산 통화 (BTC/KRW)");
                auto cur_id = std::make_unique<IdentifierExpr>();
                cur_id->name = cur.text; cur_id->pos = aname.pos;
                auto zero = std::make_unique<IntLitExpr>();
                zero->value = 0; zero->pos = aname.pos;
                auto call = std::make_unique<CallExpr>();
                call->callee = Expr{std::move(cur_id)};
                call->args.push_back(Expr{std::move(zero)});
                call->pos = aname.pos;
                record->fields.push_back(RecordField{aname.text, Expr{std::move(call)}});
            } else {
                const Token& key = expect(TokenKind::Identifier, "계약 항목 이름");
                check_dup(key.text, key.pos);
                expect(TokenKind::Colon, ":");
                Expr val = parse_expression();
                record->fields.push_back(RecordField{key.text, std::move(val)});
            }
        }
        expect(TokenKind::RBrace, "}");

        // 변수 이름 = (레코드). — 계약 = 싱글톤 전역 레코드.
        VarDeclStmt vd;
        vd.name  = name.text;
        vd.value = Expr{std::move(record)};
        vd.pos   = pos;
        prog.statements.push_back(Stmt{std::move(vd)});
    }

    // 통화 이름 [= 페그식]. — v0.4a-5c. 새 통화 선언.
    // desugar: 변수 이름 = 통화등록("이름").  (= 페그식은 a-5c 에선 파싱만, 무시.)
    Stmt parse_currency_decl() {
        Position pos = peek().pos;
        advance();  // 통화
        const Token& name = expect(TokenKind::Identifier, "통화 이름");
        if (peek().kind == TokenKind::Equals) {
            advance();
            parse_expression();   // 페그식 — a-5c 에선 무시
        }
        expect_period();
        auto reg = std::make_unique<IdentifierExpr>();
        reg->name = U"통화등록"; reg->pos = pos;
        auto lit = std::make_unique<StringLitExpr>();
        lit->value = name.text; lit->pos = pos;
        auto call = std::make_unique<CallExpr>();
        call->callee = Expr{std::move(reg)};
        call->args.push_back(Expr{std::move(lit)});
        call->pos = pos;
        VarDeclStmt vd;
        vd.name  = name.text;
        vd.value = Expr{std::move(call)};
        vd.pos   = pos;
        return Stmt{std::move(vd)};
    }

    std::vector<Stmt> parse_block() {
        expect(TokenKind::LBrace, "{");
        std::vector<Stmt> body;
        while (peek().kind != TokenKind::RBrace && peek().kind != TokenKind::EndOfFile) {
            body.push_back(parse_statement());
        }
        expect(TokenKind::RBrace, "}");
        return body;
    }

    // === Expressions: Pratt-style precedence ===
    Expr parse_expression() { return parse_or(); }

    // 'p_*' helpers build BinaryExpr for left-associative chains.
    Expr build_binary(std::u32string op, Expr lhs, Expr rhs, Position pos) {
        auto n = std::make_unique<BinaryExpr>();
        n->op  = std::move(op);
        n->lhs = std::move(lhs);
        n->rhs = std::move(rhs);
        n->pos = pos;
        return Expr{std::move(n)};
    }

    Expr parse_or() {
        Expr left = parse_and();
        while (peek().kind == TokenKind::PipePipe) {
            Position pos = peek().pos;
            advance();
            Expr right = parse_and();
            left = build_binary(U"||", std::move(left), std::move(right), pos);
        }
        return left;
    }

    Expr parse_and() {
        Expr left = parse_equality();
        while (peek().kind == TokenKind::AmpAmp) {
            Position pos = peek().pos;
            advance();
            Expr right = parse_equality();
            left = build_binary(U"&&", std::move(left), std::move(right), pos);
        }
        return left;
    }

    Expr parse_equality() {
        Expr left = parse_relational();
        while (peek().kind == TokenKind::EqEq || peek().kind == TokenKind::BangEq) {
            Position pos = peek().pos;
            std::u32string op = peek().text;
            advance();
            Expr right = parse_relational();
            left = build_binary(std::move(op), std::move(left), std::move(right), pos);
        }
        return left;
    }

    Expr parse_relational() {
        Expr left = parse_add();
        while (peek().kind == TokenKind::Lt   || peek().kind == TokenKind::Gt
            || peek().kind == TokenKind::LtEq || peek().kind == TokenKind::GtEq) {
            Position pos = peek().pos;
            std::u32string op = peek().text;
            advance();
            Expr right = parse_add();
            left = build_binary(std::move(op), std::move(left), std::move(right), pos);
        }
        return left;
    }

    // v0.2c: + - 좌결합.
    Expr parse_add() {
        Expr left = parse_mul();
        while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
            Position pos = peek().pos;
            std::u32string op = peek().text;
            advance();
            Expr right = parse_mul();
            left = build_binary(std::move(op), std::move(left), std::move(right), pos);
        }
        return left;
    }

    // v0.2c: * / % 좌결합 (산술 우선).
    Expr parse_mul() {
        Expr left = parse_unary();
        while (peek().kind == TokenKind::Star
            || peek().kind == TokenKind::Slash
            || peek().kind == TokenKind::Percent) {
            Position pos = peek().pos;
            std::u32string op = peek().text;
            advance();
            Expr right = parse_unary();
            left = build_binary(std::move(op), std::move(left), std::move(right), pos);
        }
        return left;
    }

    Expr parse_unary() {
        // v0.2c: 단항 - 와 ! 동등 우선순위.
        if (peek().kind == TokenKind::Bang || peek().kind == TokenKind::Minus) {
            Position pos = peek().pos;
            std::u32string op = peek().text;
            advance();
            Expr operand = parse_unary();
            auto n = std::make_unique<UnaryExpr>();
            n->op = std::move(op);
            n->operand = std::move(operand);
            n->pos = pos;
            return Expr{std::move(n)};
        }
        return parse_call_or_primary();
    }

    // 후위 연산: 호출 `f(…)` 과 멤버 접근 `e.필드` — 둘 다 좌결합, 교차 가능.
    Expr parse_call_or_primary() {
        Expr e = parse_primary();
        while (true) {
            if (peek().kind == TokenKind::LParen) {
                Position call_pos = peek().pos;
                advance();  // (
                std::vector<Expr> args;
                if (peek().kind != TokenKind::RParen) {
                    args.push_back(parse_expression());
                    while (peek().kind == TokenKind::Comma) {
                        advance();
                        args.push_back(parse_expression());
                    }
                }
                expect(TokenKind::RParen, ")");
                auto call = std::make_unique<CallExpr>();
                call->callee = std::move(e);
                call->args   = std::move(args);
                call->pos    = call_pos;
                e = Expr{std::move(call)};
            } else if (peek().kind == TokenKind::Dot) {
                // 멤버 접근 — 결정 #79. lexer 가 Dot/Period 를 이미 구분.
                Position dot_pos = peek().pos;
                advance();  // .
                const Token& field = expect(TokenKind::Identifier, "멤버 이름");
                auto m = std::make_unique<MemberExpr>();
                m->target = std::move(e);
                m->field  = field.text;
                m->pos    = dot_pos;
                e = Expr{std::move(m)};
            } else {
                break;
            }
        }
        return e;
    }

    // 레코드 리터럴 (키: 값, …) — 결정 #69. 호출 측에서 '(' + 식별자 + ':' 확인 후 진입.
    Expr parse_record_literal() {
        Position pos = peek().pos;
        advance();  // (
        auto rec = std::make_unique<RecordExpr>();
        rec->pos = pos;
        while (true) {
            const Token& key = expect(TokenKind::Identifier, "레코드 키 이름");
            // 중복 키 거부 — 키는 필드를 식별하므로 유일해야 함 (결정 #69).
            for (const RecordField& f : rec->fields) {
                if (f.key == key.text) {
                    std::u32string msg = U"레코드에 중복된 키: ";
                    msg += key.text;
                    raise(key.pos, msg);
                }
            }
            expect(TokenKind::Colon, ":");
            Expr val = parse_expression();
            rec->fields.push_back(RecordField{key.text, std::move(val)});
            if (peek().kind == TokenKind::Comma) { advance(); continue; }
            break;
        }
        expect(TokenKind::RParen, ")");
        return Expr{std::move(rec)};
    }

    Expr parse_primary() {
        const Token& t = peek();
        if (t.kind == TokenKind::Integer) {
            advance();
            std::string narrow = utf8::encode(t.text);
            std::int64_t value = 0;
            auto [_, ec] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), value);
            if (ec != std::errc{}) {
                raise(t.pos, "정수 변환 실패");
            }
            auto n = std::make_unique<IntLitExpr>();
            n->value = value;
            n->pos = t.pos;
            return Expr{std::move(n)};
        }
        if (t.kind == TokenKind::String) {
            advance();
            auto n = std::make_unique<StringLitExpr>();
            n->value = t.text;
            n->pos = t.pos;
            return Expr{std::move(n)};
        }
        if (t.kind == TokenKind::LParen) {
            // 레코드 리터럴 vs 괄호식 — 결정 #69·92 (3-5).
            // '(' 다음이 `식별자 :` 면 레코드, 아니면 괄호식.
            if (peek(1).kind == TokenKind::Identifier
                && peek(2).kind == TokenKind::Colon) {
                return parse_record_literal();
            }
            advance();
            Expr inner = parse_expression();
            expect(TokenKind::RParen, ")");
            return inner;
        }
        if (t.kind == TokenKind::Keyword) {
            if (t.text == U"참" || t.text == U"거짓") {
                advance();
                auto n = std::make_unique<BoolLitExpr>();
                n->value = (t.text == U"참");
                n->pos = t.pos;
                return Expr{std::move(n)};
            }
            // 위험 { 식 } — v0.3e 결정 #58.
            if (t.text == U"위험") {
                Position pos = t.pos;
                advance();
                expect(TokenKind::LBrace, "{");
                Expr inner = parse_expression();
                expect(TokenKind::RBrace, "}");
                auto n = std::make_unique<UnsafeExpr>();
                n->inner = std::move(inner);
                n->pos = pos;
                return Expr{std::move(n)};
            }
        }
        if (t.kind == TokenKind::Identifier) {
            advance();
            auto n = std::make_unique<IdentifierExpr>();
            n->name = t.text;
            n->pos = t.pos;
            return Expr{std::move(n)};
        }
        raise(t.pos, "식이 와야 할 자리에 다른 토큰이 왔습니다");
    }

    const std::vector<Token>& tokens_;
    std::size_t i_{0};
};

}  // namespace

Program parse(const std::vector<Token>& tokens) {
    Parser p(tokens);
    return p.run();
}

}  // namespace seum
