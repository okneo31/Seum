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
            prog.statements.push_back(parse_statement());
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
        if (check_keyword(U"만약"))       return parse_if();
        if (check_keyword(U"반복"))       return parse_repeat();
        if (check_keyword(U"동안"))       return parse_while();
        if (check_keyword(U"함수"))       return parse_func_decl(/*is_getter=*/false);
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

    // 함수 이름(p1, p2) -> 결과 { body } — 결정 #30.
    // 또는 지연값 이름() -> 결과 { body } — 결정 #59 (is_getter=true, 매개변수 0개).
    Stmt parse_func_decl(bool is_getter) {
        Position pos = peek().pos;
        advance();  // 함수 / 지연값
        const Token& name = expect(TokenKind::Identifier, is_getter ? "지연값 이름" : "함수 이름");
        expect(TokenKind::LParen, "(");
        std::vector<std::u32string> params;
        if (peek().kind != TokenKind::RParen) {
            params.push_back(expect(TokenKind::Identifier, "매개변수 이름").text);
            while (peek().kind == TokenKind::Comma) {
                advance();
                params.push_back(expect(TokenKind::Identifier, "매개변수 이름").text);
            }
        }
        expect(TokenKind::RParen, ")");
        if (is_getter && !params.empty()) {
            raise(pos, "지연값은 매개변수를 가질 수 없습니다");
        }
        expect(TokenKind::Arrow, "->");
        std::u32string result_name;
        if (peek().kind == TokenKind::Identifier) {
            result_name = advance().text;
        } else if (check_keyword(U"없음")) {
            result_name = U"없음";
            advance();
        } else {
            raise(peek().pos, "결과 이름 또는 '없음' 이 와야 합니다");
        }
        auto body = parse_block();
        auto node = std::make_unique<FuncDeclStmt>();
        node->name        = name.text;
        node->params      = std::move(params);
        node->result_name = std::move(result_name);
        node->body        = std::move(body);
        node->is_getter   = is_getter;
        node->pos         = pos;
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

    Expr parse_call_or_primary() {
        Expr primary = parse_primary();
        while (peek().kind == TokenKind::LParen) {
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
            call->callee = std::move(primary);
            call->args = std::move(args);
            call->pos = call_pos;
            primary = std::move(call);
        }
        return primary;
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
