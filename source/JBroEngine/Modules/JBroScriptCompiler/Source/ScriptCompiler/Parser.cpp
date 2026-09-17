#include <JBro/ScriptCompiler/Parser.h>

#include <JBro/ScriptCompiler/Lexer.h>
#include <JBro/ScriptCompiler/Token.h>

#include <utility>

namespace JBro::ScriptCompiler
{
    namespace
    {
        class ParserState final
        {
        public:
            ParserState(const SourceText& source, DiagnosticList& diagnostics)
                : m_diagnostics(diagnostics)
            {
                m_tokens = Lex(source, diagnostics);
            }

            SyntaxTree Run()
            {
                const SourceLocation begin = Current().Range.Begin;
                Array<NodeIndex> declarations;
                SkipNewlines();
                while (false == At(TokenKind::EndOfFile))
                {
                    const std::size_t before = m_position;
                    m_recovering = false;
                    const NodeIndex declaration = ParseTopLevelDeclaration();
                    if (InvalidNode != declaration)
                    {
                        declarations.Add(declaration);
                    }
                    SkipNewlines();
                    if (m_position == before)
                    {
                        Advance();
                    }
                }
                const NodeIndex root = Make(SyntaxKind::CompilationUnit, begin, std::string_view(),
                    SyntaxFlagNone, declarations);
                m_tree.SetRoot(root);
                return std::move(m_tree);
            }

        private:
            // ---- 토큰 --------------------------------------------------------------

            const Token& Current() const
            {
                return m_tokens[m_position];
            }

            TokenKind PeekKind(std::size_t ahead) const
            {
                const std::size_t index = m_position + ahead;
                return index < m_tokens.Size() ? m_tokens[index].Kind : TokenKind::EndOfFile;
            }

            bool At(TokenKind kind) const
            {
                return Current().Kind == kind;
            }

            bool AtContextual(std::string_view word) const
            {
                return At(TokenKind::Identifier) && Current().Text == word;
            }

            const Token& Advance()
            {
                const Token& token = m_tokens[m_position];
                if (false == At(TokenKind::EndOfFile))
                {
                    m_lastEnd = token.Range.End;
                    ++m_position;
                }
                return token;
            }

            void SkipNewlines()
            {
                while (At(TokenKind::Newline))
                {
                    Advance();
                }
            }

            bool AtStatementEnd() const
            {
                return At(TokenKind::Newline) || At(TokenKind::RightBrace) || At(TokenKind::EndOfFile);
            }

            // ---- 진단 --------------------------------------------------------------

            void Report(DiagnosticCode code)
            {
                if (0 != m_speculationDepth)
                {
                    m_speculationFailed = true;
                    return;
                }
                if (m_recovering)
                {
                    return;
                }
                m_diagnostics.Add(code, Current().Range);
                m_recovering = true;
            }

            void Report(DiagnosticCode code, std::string_view argument)
            {
                if (0 != m_speculationDepth)
                {
                    m_speculationFailed = true;
                    return;
                }
                if (m_recovering)
                {
                    return;
                }
                m_diagnostics.Add(code, Current().Range, argument);
                m_recovering = true;
            }

            bool Expect(TokenKind kind, std::string_view symbol)
            {
                if (At(kind))
                {
                    Advance();
                    return true;
                }
                Report(DiagnosticCode::ExpectedToken, symbol);
                return false;
            }

            // 줄 끝이나 `}` 까지 건너뛴다. 줄 끝은 먹고 `}` 는 남긴다(바깥 블록이 닫는다).
            void SyncStatement()
            {
                while (false == AtStatementEnd())
                {
                    Advance();
                }
                if (At(TokenKind::Newline))
                {
                    Advance();
                }
                m_recovering = false;
            }

            void ExpectEndOfStatement()
            {
                if (At(TokenKind::Newline))
                {
                    Advance();
                    return;
                }
                if (At(TokenKind::RightBrace) || At(TokenKind::EndOfFile))
                {
                    return;
                }
                Report(DiagnosticCode::ExpectedEndOfStatement);
                SyncStatement();
            }

            // ---- 노드 --------------------------------------------------------------

            NodeIndex Make(SyntaxKind kind, const SourceLocation& begin, std::string_view text,
                std::uint16_t flags, const Array<NodeIndex>& children)
            {
                return m_tree.AddNode(kind, MakeRange(begin), text, flags,
                    ArrayView<const NodeIndex>(children.Data(), children.Size()));
            }

            NodeIndex Make(SyntaxKind kind, const SourceLocation& begin, std::string_view text,
                std::uint16_t flags, std::initializer_list<NodeIndex> children)
            {
                return m_tree.AddNode(kind, MakeRange(begin), text, flags,
                    ArrayView<const NodeIndex>(children.begin(), children.size()));
            }

            SourceRange MakeRange(const SourceLocation& begin) const
            {
                SourceRange range{ begin, m_lastEnd };
                if (range.End.Offset < range.Begin.Offset)
                {
                    range.End = range.Begin;
                }
                return range;
            }

            SourceLocation BeginOf(NodeIndex node) const
            {
                return m_tree.Get(node).Range.Begin;
            }

            // ---- 추측 --------------------------------------------------------------

            // 에러를 내지 않고 읽어 본 뒤 제자리로 돌아온다. 그 사이에 만든 노드도 버린다.
            struct Checkpoint
            {
                std::size_t Position;
                SourceLocation LastEnd;
                std::size_t NodeCount;
                std::size_t ChildCount;
                bool Failed;
            };

            Checkpoint BeginSpeculation()
            {
                Checkpoint checkpoint{ m_position, m_lastEnd, m_tree.GetNodeCount(), m_tree.GetChildIndexCount(),
                    m_speculationFailed };
                ++m_speculationDepth;
                m_speculationFailed = false;
                return checkpoint;
            }

            bool EndSpeculation(const Checkpoint& checkpoint, bool succeeded)
            {
                const bool ok = succeeded && false == m_speculationFailed;
                --m_speculationDepth;
                m_speculationFailed = checkpoint.Failed;
                m_position = checkpoint.Position;
                m_lastEnd = checkpoint.LastEnd;
                m_tree.Truncate(checkpoint.NodeCount, checkpoint.ChildCount);
                return ok;
            }

            // ---- 선언 --------------------------------------------------------------

            NodeIndex ParseTopLevelDeclaration()
            {
                switch (Current().Kind)
                {
                case TokenKind::KeywordScript:
                case TokenKind::KeywordClass:
                case TokenKind::KeywordStruct:
                case TokenKind::KeywordInterface:
                    return ParseTypeDeclaration();
                case TokenKind::KeywordEnum:
                    return ParseEnumDeclaration();
                default:
                    Report(DiagnosticCode::ExpectedDeclaration);
                    SyncToDeclaration();
                    return InvalidNode;
                }
            }

            static bool IsDeclarationKeyword(TokenKind kind)
            {
                return TokenKind::KeywordScript == kind || TokenKind::KeywordClass == kind
                    || TokenKind::KeywordStruct == kind || TokenKind::KeywordInterface == kind
                    || TokenKind::KeywordEnum == kind;
            }

            // 줄을 여는 선언 키워드까지 건너뛴다.
            void SyncToDeclaration()
            {
                while (false == At(TokenKind::EndOfFile))
                {
                    Advance();
                    const bool lineStart = TokenKind::Newline == m_tokens[m_position - 1].Kind;
                    if (lineStart && IsDeclarationKeyword(Current().Kind))
                    {
                        break;
                    }
                }
                m_recovering = false;
            }

            NodeIndex ParseTypeDeclaration()
            {
                const SourceLocation begin = Current().Range.Begin;
                const TokenKind keyword = Advance().Kind;
                SyntaxKind kind = SyntaxKind::ScriptDeclaration;
                switch (keyword)
                {
                case TokenKind::KeywordClass: kind = SyntaxKind::ClassDeclaration; break;
                case TokenKind::KeywordStruct: kind = SyntaxKind::StructDeclaration; break;
                case TokenKind::KeywordInterface: kind = SyntaxKind::InterfaceDeclaration; break;
                default: break;
                }

                std::string_view name;
                if (At(TokenKind::Identifier))
                {
                    name = Advance().Text;
                }
                else
                {
                    Report(DiagnosticCode::ExpectedName);
                }

                Array<NodeIndex> children;
                children.Add(InvalidNode);
                if (At(TokenKind::Colon))
                {
                    const SourceLocation baseBegin = Current().Range.Begin;
                    Advance();
                    Array<NodeIndex> bases;
                    do
                    {
                        const NodeIndex base = ParseType();
                        if (InvalidNode != base)
                        {
                            bases.Add(base);
                        }
                    } while (At(TokenKind::Comma) && (Advance(), true));
                    children[0] = Make(SyntaxKind::BaseList, baseBegin, std::string_view(), SyntaxFlagNone, bases);
                }

                SkipNewlines();
                if (false == Expect(TokenKind::LeftBrace, "{"))
                {
                    SyncStatement();
                    return Make(kind, begin, name, SyntaxFlagNone, children);
                }
                ParseMembers(children);
                return Make(kind, begin, name, SyntaxFlagNone, children);
            }

            void ParseMembers(Array<NodeIndex>& children)
            {
                while (true)
                {
                    SkipNewlines();
                    if (At(TokenKind::RightBrace))
                    {
                        Advance();
                        return;
                    }
                    if (At(TokenKind::EndOfFile))
                    {
                        Report(DiagnosticCode::ExpectedToken, "}");
                        return;
                    }
                    const std::size_t before = m_position;
                    m_recovering = false;
                    const NodeIndex member = ParseMember();
                    if (InvalidNode != member)
                    {
                        children.Add(member);
                    }
                    if (m_position == before)
                    {
                        Advance();
                    }
                }
            }

            NodeIndex ParseEnumDeclaration()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance();
                std::string_view name;
                if (At(TokenKind::Identifier))
                {
                    name = Advance().Text;
                }
                else
                {
                    Report(DiagnosticCode::ExpectedName);
                }
                Array<NodeIndex> members;
                SkipNewlines();
                if (false == Expect(TokenKind::LeftBrace, "{"))
                {
                    SyncStatement();
                    return Make(SyntaxKind::EnumDeclaration, begin, name, SyntaxFlagNone, members);
                }
                while (true)
                {
                    SkipNewlines();
                    if (At(TokenKind::RightBrace))
                    {
                        Advance();
                        break;
                    }
                    if (At(TokenKind::EndOfFile))
                    {
                        Report(DiagnosticCode::ExpectedToken, "}");
                        break;
                    }
                    m_recovering = false;
                    if (false == At(TokenKind::Identifier))
                    {
                        Report(DiagnosticCode::ExpectedName);
                        SyncStatement();
                        continue;
                    }
                    const SourceLocation memberBegin = Current().Range.Begin;
                    const std::string_view memberName = Advance().Text;
                    members.Add(Make(SyntaxKind::EnumMember, memberBegin, memberName, SyntaxFlagNone, {}));
                    // 멤버는 한 줄에 하나다.
                    ExpectEndOfStatement();
                }
                return Make(SyntaxKind::EnumDeclaration, begin, name, SyntaxFlagNone, members);
            }

            NodeIndex ParseMember()
            {
                const SourceLocation begin = Current().Range.Begin;
                NodeIndex attributes = InvalidNode;
                if (At(TokenKind::LeftBracket))
                {
                    attributes = ParseAttributes();
                    SkipNewlines();
                }

                std::uint16_t flags = SyntaxFlagNone;
                while (true)
                {
                    if (At(TokenKind::KeywordPublic))
                    {
                        flags |= SyntaxFlagPublic;
                    }
                    else if (At(TokenKind::KeywordProtected))
                    {
                        flags |= SyntaxFlagProtected;
                    }
                    else if (At(TokenKind::KeywordPrivate))
                    {
                        flags |= SyntaxFlagPrivate;
                    }
                    else if (At(TokenKind::KeywordStatic))
                    {
                        flags |= SyntaxFlagStatic;
                    }
                    else
                    {
                        break;
                    }
                    Advance();
                }

                if (At(TokenKind::KeywordFn))
                {
                    return ParseFunction(begin, attributes, flags);
                }
                // `public MaxHp = 10` - 타입이 빠졌다(jbroc-rules §3.2).
                if (At(TokenKind::Identifier) && TokenKind::Equal == PeekKind(1))
                {
                    Report(DiagnosticCode::MissingFieldType, Current().Text);
                    SyncStatement();
                    return InvalidNode;
                }
                if (false == StartsType())
                {
                    Report(DiagnosticCode::ExpectedMember);
                    SyncStatement();
                    return InvalidNode;
                }

                const NodeIndex type = ParseType();
                if (false == At(TokenKind::Identifier))
                {
                    Report(DiagnosticCode::ExpectedName);
                    SyncStatement();
                    return InvalidNode;
                }
                const std::string_view name = Advance().Text;
                NodeIndex initializer = InvalidNode;
                if (At(TokenKind::Equal))
                {
                    Advance();
                    initializer = ParseExpression();
                }
                const NodeIndex field = Make(SyntaxKind::FieldDeclaration, begin, name, flags,
                    { attributes, type, initializer });
                ExpectEndOfStatement();
                return field;
            }

            NodeIndex ParseAttributes()
            {
                const SourceLocation begin = Current().Range.Begin;
                Array<NodeIndex> attributes;
                while (At(TokenKind::LeftBracket))
                {
                    Advance();
                    do
                    {
                        const SourceLocation attributeBegin = Current().Range.Begin;
                        if (false == At(TokenKind::Identifier))
                        {
                            Report(DiagnosticCode::ExpectedName);
                            break;
                        }
                        const std::string_view name = Advance().Text;
                        Array<NodeIndex> arguments;
                        if (At(TokenKind::LeftParen))
                        {
                            Advance();
                            if (false == At(TokenKind::RightParen))
                            {
                                do
                                {
                                    const NodeIndex argument = ParseExpression();
                                    if (InvalidNode != argument)
                                    {
                                        arguments.Add(argument);
                                    }
                                } while (At(TokenKind::Comma) && (Advance(), true));
                            }
                            Expect(TokenKind::RightParen, ")");
                        }
                        attributes.Add(Make(SyntaxKind::Attribute, attributeBegin, name, SyntaxFlagNone, arguments));
                    } while (At(TokenKind::Comma) && (Advance(), true));
                    Expect(TokenKind::RightBracket, "]");
                }
                return Make(SyntaxKind::AttributeList, begin, std::string_view(), SyntaxFlagNone, attributes);
            }

            NodeIndex ParseFunction(const SourceLocation& begin, NodeIndex attributes, std::uint16_t flags)
            {
                Advance(); // fn
                std::string_view name;
                if (At(TokenKind::Identifier))
                {
                    name = Advance().Text;
                }
                else
                {
                    Report(DiagnosticCode::ExpectedName);
                }
                const NodeIndex parameters = ParseParameterList();
                NodeIndex returnType = InvalidNode;
                if (At(TokenKind::Arrow))
                {
                    Advance();
                    returnType = ParseType();
                }
                // 접미사는 함수 선언 끝에서만 키워드다(jbroscript-syntax §2.1).
                while (true)
                {
                    if (AtContextual("callback"))
                    {
                        flags |= SyntaxFlagCallback;
                    }
                    else if (AtContextual("override"))
                    {
                        flags |= SyntaxFlagOverride;
                    }
                    else if (AtContextual("require"))
                    {
                        flags |= SyntaxFlagRequire;
                    }
                    else
                    {
                        break;
                    }
                    Advance();
                }
                if (false == AtStatementEnd() && false == At(TokenKind::LeftBrace))
                {
                    Report(DiagnosticCode::ExpectedEndOfStatement);
                    SyncStatement();
                }

                // 본문은 다음 줄의 `{` 다. 없으면 선언만 있는 함수다(interface, require).
                NodeIndex body = InvalidNode;
                const std::size_t beforeNewlines = m_position;
                const SourceLocation lastEndBeforeNewlines = m_lastEnd;
                SkipNewlines();
                if (At(TokenKind::LeftBrace))
                {
                    body = ParseBlock();
                }
                else
                {
                    m_position = beforeNewlines;
                    m_lastEnd = lastEndBeforeNewlines;
                }
                return Make(SyntaxKind::FunctionDeclaration, begin, name, flags,
                    { attributes, parameters, returnType, body });
            }

            NodeIndex ParseParameterList()
            {
                const SourceLocation begin = Current().Range.Begin;
                Array<NodeIndex> parameters;
                if (false == Expect(TokenKind::LeftParen, "("))
                {
                    return Make(SyntaxKind::ParameterList, begin, std::string_view(), SyntaxFlagNone, parameters);
                }
                if (false == At(TokenKind::RightParen))
                {
                    do
                    {
                        const SourceLocation parameterBegin = Current().Range.Begin;
                        const NodeIndex type = ParseType();
                        if (false == At(TokenKind::Identifier))
                        {
                            Report(DiagnosticCode::ExpectedName);
                            break;
                        }
                        const std::string_view name = Advance().Text;
                        parameters.Add(Make(SyntaxKind::Parameter, parameterBegin, name, SyntaxFlagNone, { type }));
                    } while (At(TokenKind::Comma) && (Advance(), true));
                }
                Expect(TokenKind::RightParen, ")");
                return Make(SyntaxKind::ParameterList, begin, std::string_view(), SyntaxFlagNone, parameters);
            }

            // ---- 타입 --------------------------------------------------------------

            bool StartsType() const
            {
                return At(TokenKind::KeywordRef) || At(TokenKind::KeywordConst) || At(TokenKind::Identifier);
            }

            NodeIndex ParseType()
            {
                const SourceLocation begin = Current().Range.Begin;
                std::uint16_t flags = SyntaxFlagNone;
                if (At(TokenKind::KeywordRef))
                {
                    Advance();
                    flags |= SyntaxFlagRef;
                }
                if (At(TokenKind::KeywordConst))
                {
                    Advance();
                    flags |= SyntaxFlagConst;
                }
                if (false == At(TokenKind::Identifier))
                {
                    Report(DiagnosticCode::ExpectedType);
                    return InvalidNode;
                }
                const std::string_view name = Advance().Text;
                Array<NodeIndex> arguments;
                if (At(TokenKind::Less))
                {
                    Advance();
                    do
                    {
                        const NodeIndex argument = ParseType();
                        if (InvalidNode == argument)
                        {
                            break;
                        }
                        arguments.Add(argument);
                    } while (At(TokenKind::Comma) && (Advance(), true));
                    Expect(TokenKind::Greater, ">");
                }
                return Make(SyntaxKind::TypeName, begin, name, flags, arguments);
            }

            // 문장이 "타입 + 이름" 으로 시작하는가.
            bool AtLocalDeclaration()
            {
                if (At(TokenKind::KeywordRef) || At(TokenKind::KeywordConst))
                {
                    return true;
                }
                if (false == At(TokenKind::Identifier))
                {
                    return false;
                }
                // 흔한 경우는 추측 없이 가른다.
                const TokenKind next = PeekKind(1);
                if (TokenKind::Identifier == next)
                {
                    return true;
                }
                if (TokenKind::Less != next)
                {
                    return false;
                }
                const Checkpoint checkpoint = BeginSpeculation();
                const NodeIndex type = ParseType();
                const bool succeeded = InvalidNode != type && At(TokenKind::Identifier);
                return EndSpeculation(checkpoint, succeeded);
            }

            // ---- 문장 --------------------------------------------------------------

            NodeIndex ParseBlock()
            {
                const SourceLocation begin = Current().Range.Begin;
                Array<NodeIndex> statements;
                Advance(); // {
                while (true)
                {
                    SkipNewlines();
                    if (At(TokenKind::RightBrace))
                    {
                        Advance();
                        break;
                    }
                    if (At(TokenKind::EndOfFile))
                    {
                        Report(DiagnosticCode::ExpectedToken, "}");
                        break;
                    }
                    const std::size_t before = m_position;
                    m_recovering = false;
                    const NodeIndex statement = ParseStatement();
                    if (InvalidNode != statement)
                    {
                        statements.Add(statement);
                    }
                    if (m_position == before)
                    {
                        Advance();
                    }
                }
                return Make(SyntaxKind::Block, begin, std::string_view(), SyntaxFlagNone, statements);
            }

            // 조건 뒤나 else 뒤의 본문이다. 한 줄 본문은 없다(jbroscript-syntax §11.2).
            NodeIndex ParseRequiredBlock()
            {
                SkipNewlines();
                if (At(TokenKind::LeftBrace))
                {
                    return ParseBlock();
                }
                Report(DiagnosticCode::ExpectedBlock);
                SyncStatement();
                return InvalidNode;
            }

            NodeIndex ParseStatement()
            {
                switch (Current().Kind)
                {
                case TokenKind::LeftBrace:
                    return ParseBlock();
                case TokenKind::KeywordIf:
                    return ParseIf();
                case TokenKind::KeywordWhile:
                    return ParseWhile();
                case TokenKind::KeywordFor:
                    return ParseFor();
                case TokenKind::KeywordSwitch:
                    return ParseSwitch();
                case TokenKind::KeywordReturn:
                    return ParseReturn();
                case TokenKind::KeywordBreak:
                    return ParseJump(SyntaxKind::BreakStatement);
                case TokenKind::KeywordContinue:
                    return ParseJump(SyntaxKind::ContinueStatement);
                case TokenKind::KeywordElse:
                case TokenKind::KeywordCase:
                case TokenKind::KeywordDefault:
                    Report(DiagnosticCode::ExpectedStatement);
                    SyncStatement();
                    return InvalidNode;
                default:
                    break;
                }

                if (AtLocalDeclaration())
                {
                    return ParseLocalDeclaration();
                }

                const SourceLocation begin = Current().Range.Begin;
                const NodeIndex expression = ParseExpression();
                if (InvalidNode == expression)
                {
                    SyncStatement();
                    return InvalidNode;
                }
                if (At(TokenKind::Equal) || At(TokenKind::PlusEqual) || At(TokenKind::MinusEqual)
                    || At(TokenKind::StarEqual) || At(TokenKind::SlashEqual))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex value = ParseExpression();
                    const NodeIndex assignment = Make(SyntaxKind::AssignmentStatement, begin, op, SyntaxFlagNone,
                        { expression, value });
                    ExpectEndOfStatement();
                    return assignment;
                }
                const NodeIndex statement = Make(SyntaxKind::ExpressionStatement, begin, std::string_view(),
                    SyntaxFlagNone, { expression });
                ExpectEndOfStatement();
                return statement;
            }

            NodeIndex ParseLocalDeclaration()
            {
                const SourceLocation begin = Current().Range.Begin;
                const NodeIndex type = ParseType();
                if (false == At(TokenKind::Identifier))
                {
                    Report(DiagnosticCode::ExpectedName);
                    SyncStatement();
                    return InvalidNode;
                }
                const std::string_view name = Advance().Text;
                NodeIndex initializer = InvalidNode;
                if (At(TokenKind::Equal))
                {
                    Advance();
                    initializer = ParseExpression();
                }
                const NodeIndex declaration = Make(SyntaxKind::LocalDeclaration, begin, name, SyntaxFlagNone,
                    { type, initializer });
                ExpectEndOfStatement();
                return declaration;
            }

            // if·while·switch 의 조건은 괄호로 감싼다(jbroscript-syntax §11.1).
            NodeIndex ParseCondition(std::string_view keyword)
            {
                if (false == At(TokenKind::LeftParen))
                {
                    Report(DiagnosticCode::MissingConditionParentheses, keyword);
                    return ParseExpression();
                }
                Advance();
                const NodeIndex condition = ParseExpression();
                Expect(TokenKind::RightParen, ")");
                return condition;
            }

            NodeIndex ParseIf()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance(); // if
                const NodeIndex condition = ParseCondition("if");
                const NodeIndex body = ParseRequiredBlock();
                NodeIndex elseNode = InvalidNode;

                // else 는 다음 줄에 온다. 없으면 줄바꿈을 먹지 않고 되돌린다 - 그 줄바꿈이 이 문장의 끝이다.
                const std::size_t beforeNewlines = m_position;
                const SourceLocation lastEndBeforeNewlines = m_lastEnd;
                SkipNewlines();
                if (At(TokenKind::KeywordElse))
                {
                    Advance();
                    elseNode = At(TokenKind::KeywordIf) ? ParseIf() : ParseRequiredBlock();
                }
                else
                {
                    m_position = beforeNewlines;
                    m_lastEnd = lastEndBeforeNewlines;
                }
                return Make(SyntaxKind::IfStatement, begin, std::string_view(), SyntaxFlagNone,
                    { condition, body, elseNode });
            }

            NodeIndex ParseWhile()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance(); // while
                const NodeIndex condition = ParseCondition("while");
                const NodeIndex body = ParseRequiredBlock();
                return Make(SyntaxKind::WhileStatement, begin, std::string_view(), SyntaxFlagNone,
                    { condition, body });
            }

            NodeIndex ParseFor()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance(); // for
                if (false == At(TokenKind::LeftParen))
                {
                    Report(DiagnosticCode::MissingConditionParentheses, "for");
                    SyncStatement();
                    return InvalidNode;
                }
                Advance();
                const NodeIndex first = ParseForVariable();
                NodeIndex second = InvalidNode;
                if (At(TokenKind::Comma))
                {
                    Advance();
                    second = ParseForVariable();
                }
                // in 은 for 괄호 안에서만 키워드다.
                if (AtContextual("in"))
                {
                    Advance();
                }
                else
                {
                    Report(DiagnosticCode::ExpectedIn);
                }
                const NodeIndex iterable = ParseExpression();
                Expect(TokenKind::RightParen, ")");
                const NodeIndex body = ParseRequiredBlock();
                return Make(SyntaxKind::ForStatement, begin, std::string_view(), SyntaxFlagNone,
                    { first, second, iterable, body });
            }

            NodeIndex ParseForVariable()
            {
                const SourceLocation begin = Current().Range.Begin;
                std::uint16_t flags = SyntaxFlagNone;
                if (At(TokenKind::KeywordRef))
                {
                    Advance();
                    flags |= SyntaxFlagRef;
                }
                if (false == At(TokenKind::Identifier))
                {
                    Report(DiagnosticCode::ExpectedName);
                    return InvalidNode;
                }
                const std::string_view name = Advance().Text;
                return Make(SyntaxKind::ForVariable, begin, name, flags, {});
            }

            NodeIndex ParseSwitch()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance(); // switch
                Array<NodeIndex> children;
                children.Add(ParseCondition("switch"));
                SkipNewlines();
                if (false == Expect(TokenKind::LeftBrace, "{"))
                {
                    SyncStatement();
                    return Make(SyntaxKind::SwitchStatement, begin, std::string_view(), SyntaxFlagNone, children);
                }
                while (true)
                {
                    SkipNewlines();
                    if (At(TokenKind::RightBrace))
                    {
                        Advance();
                        break;
                    }
                    if (At(TokenKind::EndOfFile))
                    {
                        Report(DiagnosticCode::ExpectedToken, "}");
                        break;
                    }
                    m_recovering = false;
                    const SourceLocation clauseBegin = Current().Range.Begin;
                    if (At(TokenKind::KeywordCase))
                    {
                        Advance();
                        Array<NodeIndex> clause;
                        do
                        {
                            const NodeIndex value = ParseExpression();
                            if (InvalidNode != value)
                            {
                                clause.Add(value);
                            }
                        } while (At(TokenKind::Comma) && (Advance(), true));
                        clause.Add(ParseRequiredBlock());
                        children.Add(Make(SyntaxKind::CaseClause, clauseBegin, std::string_view(), SyntaxFlagNone, clause));
                    }
                    else if (At(TokenKind::KeywordDefault))
                    {
                        Advance();
                        const NodeIndex body = ParseRequiredBlock();
                        children.Add(Make(SyntaxKind::DefaultClause, clauseBegin, std::string_view(), SyntaxFlagNone,
                            { body }));
                    }
                    else
                    {
                        const std::size_t before = m_position;
                        Report(DiagnosticCode::ExpectedCaseOrDefault);
                        SyncStatement();
                        if (m_position == before)
                        {
                            Advance();
                        }
                    }
                }
                return Make(SyntaxKind::SwitchStatement, begin, std::string_view(), SyntaxFlagNone, children);
            }

            NodeIndex ParseReturn()
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance(); // return
                NodeIndex value = InvalidNode;
                if (false == AtStatementEnd())
                {
                    value = ParseExpression();
                }
                const NodeIndex statement = Make(SyntaxKind::ReturnStatement, begin, std::string_view(),
                    SyntaxFlagNone, { value });
                ExpectEndOfStatement();
                return statement;
            }

            NodeIndex ParseJump(SyntaxKind kind)
            {
                const SourceLocation begin = Current().Range.Begin;
                Advance();
                const NodeIndex statement = Make(kind, begin, std::string_view(), SyntaxFlagNone, {});
                ExpectEndOfStatement();
                return statement;
            }

            // ---- 식 ----------------------------------------------------------------
            // 우선순위는 jbroscript-syntax §10.1 이다. 범위 `..` 는 그 표보다 낮다.

            NodeIndex ParseExpression()
            {
                const NodeIndex left = ParseOr();
                if (InvalidNode == left || false == At(TokenKind::DotDot))
                {
                    return left;
                }
                const SourceLocation begin = BeginOf(left);
                Advance();
                const NodeIndex right = ParseOr();
                return Make(SyntaxKind::RangeExpression, begin, std::string_view(), SyntaxFlagNone, { left, right });
            }

            NodeIndex ParseOr()
            {
                NodeIndex left = ParseAnd();
                while (InvalidNode != left && At(TokenKind::KeywordOr))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex right = ParseAnd();
                    left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                }
                return left;
            }

            NodeIndex ParseAnd()
            {
                NodeIndex left = ParseEquality();
                while (InvalidNode != left && At(TokenKind::KeywordAnd))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex right = ParseEquality();
                    left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                }
                return left;
            }

            NodeIndex ParseEquality()
            {
                NodeIndex left = ParseRelational();
                while (InvalidNode != left)
                {
                    if (At(TokenKind::EqualEqual) || At(TokenKind::BangEqual))
                    {
                        const std::string_view op = Advance().Text;
                        const NodeIndex right = ParseRelational();
                        left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                        continue;
                    }
                    if (At(TokenKind::KeywordIs))
                    {
                        Advance();
                        std::uint16_t flags = SyntaxFlagNone;
                        if (At(TokenKind::KeywordNot))
                        {
                            Advance();
                            flags |= SyntaxFlagNegated;
                        }
                        if (At(TokenKind::KeywordNull))
                        {
                            Advance();
                        }
                        else
                        {
                            Report(DiagnosticCode::ExpectedNullAfterIs);
                        }
                        left = Make(SyntaxKind::IsNullExpression, BeginOf(left), std::string_view(), flags, { left });
                        continue;
                    }
                    break;
                }
                return left;
            }

            NodeIndex ParseRelational()
            {
                NodeIndex left = ParseAdditive();
                while (InvalidNode != left && (At(TokenKind::Less) || At(TokenKind::LessEqual)
                    || At(TokenKind::Greater) || At(TokenKind::GreaterEqual)))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex right = ParseAdditive();
                    left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                }
                return left;
            }

            NodeIndex ParseAdditive()
            {
                NodeIndex left = ParseMultiplicative();
                while (InvalidNode != left && (At(TokenKind::Plus) || At(TokenKind::Minus)))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex right = ParseMultiplicative();
                    left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                }
                return left;
            }

            NodeIndex ParseMultiplicative()
            {
                NodeIndex left = ParseUnary();
                while (InvalidNode != left && (At(TokenKind::Star) || At(TokenKind::Slash) || At(TokenKind::Percent)))
                {
                    const std::string_view op = Advance().Text;
                    const NodeIndex right = ParseUnary();
                    left = Make(SyntaxKind::BinaryExpression, BeginOf(left), op, SyntaxFlagNone, { left, right });
                }
                return left;
            }

            NodeIndex ParseUnary()
            {
                if (At(TokenKind::Minus) || At(TokenKind::KeywordNot))
                {
                    const SourceLocation begin = Current().Range.Begin;
                    const std::string_view op = Advance().Text;
                    const NodeIndex operand = ParseUnary();
                    return Make(SyntaxKind::UnaryExpression, begin, op, SyntaxFlagNone, { operand });
                }
                return ParsePostfix();
            }

            NodeIndex ParsePostfix()
            {
                NodeIndex expression = ParsePrimary();
                while (InvalidNode != expression)
                {
                    const SourceLocation begin = BeginOf(expression);
                    if (At(TokenKind::Dot))
                    {
                        Advance();
                        if (false == At(TokenKind::Identifier))
                        {
                            Report(DiagnosticCode::ExpectedName);
                            break;
                        }
                        const std::string_view member = Advance().Text;
                        expression = Make(SyntaxKind::MemberAccessExpression, begin, member, SyntaxFlagNone, { expression });
                        continue;
                    }
                    if (At(TokenKind::LeftParen))
                    {
                        Advance();
                        Array<NodeIndex> children;
                        children.Add(expression);
                        if (false == At(TokenKind::RightParen))
                        {
                            do
                            {
                                const NodeIndex argument = ParseArgument();
                                if (InvalidNode != argument)
                                {
                                    children.Add(argument);
                                }
                            } while (At(TokenKind::Comma) && (Advance(), true));
                        }
                        Expect(TokenKind::RightParen, ")");
                        expression = Make(SyntaxKind::CallExpression, begin, std::string_view(), SyntaxFlagNone, children);
                        continue;
                    }
                    if (At(TokenKind::LeftBracket))
                    {
                        Advance();
                        const NodeIndex index = ParseExpression();
                        Expect(TokenKind::RightBracket, "]");
                        expression = Make(SyntaxKind::IndexExpression, begin, std::string_view(), SyntaxFlagNone,
                            { expression, index });
                        continue;
                    }
                    break;
                }
                return expression;
            }

            // 호출하는 쪽의 `ref hit` 다(jbroscript-syntax §8.1).
            NodeIndex ParseArgument()
            {
                if (At(TokenKind::KeywordRef))
                {
                    const SourceLocation begin = Current().Range.Begin;
                    Advance();
                    const NodeIndex value = ParseExpression();
                    return Make(SyntaxKind::RefArgument, begin, std::string_view(), SyntaxFlagNone, { value });
                }
                return ParseExpression();
            }

            // `Name<` 뒤가 타입 인자로 읽히고 `>` 바로 뒤에 `(` 가 오는가.
            bool AtGenericCall()
            {
                const Checkpoint checkpoint = BeginSpeculation();
                Advance(); // <
                bool succeeded = true;
                do
                {
                    if (InvalidNode == ParseType())
                    {
                        succeeded = false;
                        break;
                    }
                } while (At(TokenKind::Comma) && (Advance(), true));
                succeeded = succeeded && At(TokenKind::Greater);
                if (succeeded)
                {
                    Advance();
                    succeeded = At(TokenKind::LeftParen);
                }
                return EndSpeculation(checkpoint, succeeded);
            }

            NodeIndex ParsePrimary()
            {
                const SourceLocation begin = Current().Range.Begin;
                switch (Current().Kind)
                {
                case TokenKind::Identifier:
                {
                    const std::string_view name = Advance().Text;
                    if (At(TokenKind::Less) && AtGenericCall())
                    {
                        Advance(); // <
                        Array<NodeIndex> arguments;
                        do
                        {
                            arguments.Add(ParseType());
                        } while (At(TokenKind::Comma) && (Advance(), true));
                        Expect(TokenKind::Greater, ">");
                        return Make(SyntaxKind::GenericNameExpression, begin, name, SyntaxFlagNone, arguments);
                    }
                    return Make(SyntaxKind::NameExpression, begin, name, SyntaxFlagNone, {});
                }
                case TokenKind::IntegerLiteral:
                    return MakeLiteral(SyntaxKind::IntegerLiteral, begin);
                case TokenKind::FloatLiteral:
                    return MakeLiteral(SyntaxKind::FloatLiteral, begin);
                case TokenKind::StringLiteral:
                    return MakeLiteral(SyntaxKind::StringLiteral, begin);
                case TokenKind::KeywordTrue:
                case TokenKind::KeywordFalse:
                    return MakeLiteral(SyntaxKind::BooleanLiteral, begin);
                case TokenKind::KeywordNull:
                    Advance();
                    return Make(SyntaxKind::NullLiteral, begin, std::string_view(), SyntaxFlagNone, {});
                case TokenKind::LeftParen:
                {
                    Advance();
                    const NodeIndex inner = ParseExpression();
                    Expect(TokenKind::RightParen, ")");
                    return Make(SyntaxKind::ParenthesizedExpression, begin, std::string_view(), SyntaxFlagNone, { inner });
                }
                default:
                    Report(DiagnosticCode::ExpectedExpression);
                    return InvalidNode;
                }
            }

            NodeIndex MakeLiteral(SyntaxKind kind, const SourceLocation& begin)
            {
                const std::string_view text = Advance().Text;
                return Make(kind, begin, text, SyntaxFlagNone, {});
            }

            DiagnosticList& m_diagnostics;
            Array<Token> m_tokens;
            SyntaxTree m_tree;
            std::size_t m_position = 0;
            SourceLocation m_lastEnd;
            // 한 문장에서 첫 에러를 낸 뒤 다시 읽기 시작할 때까지 참이다. 그동안의 에러는 내지 않는다.
            bool m_recovering = false;
            std::uint32_t m_speculationDepth = 0;
            bool m_speculationFailed = false;
        };
    }

    SyntaxTree Parse(const SourceText& source, DiagnosticList& diagnostics)
    {
        ParserState state(source, diagnostics);
        return state.Run();
    }
}
