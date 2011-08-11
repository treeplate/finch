#include <sstream>

#include "ArrayExpr.h"
#include "BindExpr.h"
#include "BlockExpr.h"
#include "FinchParser.h"
#include "IErrorReporter.h"
#include "ILineReader.h"
#include "MessageExpr.h"
#include "NameExpr.h"
#include "NumberExpr.h"
#include "ObjectExpr.h"
#include "SelfExpr.h"
#include "SequenceExpr.h"
#include "SetExpr.h"
#include "StringExpr.h"
#include "UndefineExpr.h"
#include "VarExpr.h"

// Recursive descent parsers are generally pretty straightforward to read. Each
// grammar production is a function call and they call each other directly so
// the code ends up looking pretty much like the grammar.
//
// The only trouble is when a parse error occurs. Since a recursive descent
// parser uses the native callstack to maintain the parse state, we've got to
// unwind the stack to get back to ground zero. This means either using
// exceptions (which I'm not a fan of in C++) or having every call to a grammar
// expression function check the return code for failure and return again. So,
// instead of the nice:
//
//  Ref<Expr> Operator()
//  {
//      Ref<Expr> left = Unary();
//      String op = Match(TOKEN_OPERATOR).Text();
//      Ref<Expr> right = Unary();
//      
//      return Ref<Expr>(new OperatorExpr(left, op, right));
//  }
//
// We end up having to do:
//
//  Ref<Expr> Operator()
//  {
//      Ref<Expr> left = Unary();
//      if (left.IsNull()) return Ref<Expr>(); // unwind
//      if (!Lookahead(TOKEN_OPERATOR)) return ParseError("Need operator.");
//      String op = Match(TOKEN_OPERATOR).Text();
//      Ref<Expr> right = Unary();
//      if (right.IsNull()) return Ref<Expr>(); // unwind
//      
//      return Ref<Expr>(new OperatorExpr(left, op, right));
//  }
//
// To get around that, we'll define some macros that do this checking and
// unwinding for us. They're a little magical (hence this long explanation),
// but should make the code easier to read.

#define PARSE_RULE(_result, _func) \
    Ref<Expr> _result = _func; \
    if (_result.IsNull()) return _result;

// Check that the next token is the expected type, but don't consume it. Bail
// with an error if the type doesn't match.
#define EXPECT(_token, _message) \
    if (!LookAhead(_token)) return ParseError(_message);

// Consume the next token of a required type. Bail and return an error if the
// token doesn't match the expected type.
#define CONSUME(_token, _message) \
    if (!Match(_token)) return ParseError(_message);

// Consume and discard the next token if it's the given type. Lets us eat
// optional tokens.
#define IGNORE(_token) Match(_token);

namespace Finch
{    
    Ref<Expr> FinchParser::Parse()
    {
        if (IsInfinite())
        {
            // skip past Sequence() otherwise we'll keep reading lines forever
            PARSE_RULE(expr, Bind());
            IGNORE(TOKEN_LINE);
            
            return expr;
        }
        else
        {
            // since expression includes sequence expressions, this will parse
            // as many lines as we have
            PARSE_RULE(expr, Expression());
            EXPECT(TOKEN_EOF, "Parser ended unexpectedly before reaching end of file.");
            
            return expr;
        }
    }
    
    Ref<Expr> FinchParser::Expression()
    {
        PARSE_RULE(expr, Sequence());
        IGNORE(TOKEN_LINE);
        
        return expr;
    }
    
    Ref<Expr> FinchParser::Sequence()
    {
        Array<Ref<Expr> > expressions;
        PARSE_RULE(dummy, ParseSequence(expressions));
        
        // if there's just one, don't wrap it in a sequence
        if (expressions.Count() == 1) return expressions[0];
        
        return Ref<Expr>(new SequenceExpr(expressions));
    }
    
    Ref<Expr> FinchParser::Bind()
    {
        PARSE_RULE(target, Assignment());
        
        if (Match(TOKEN_BIND))
        {
            Ref<Expr> expr = Ref<Expr>(new BindExpr(target));

            if (Match(TOKEN_LEFT_PAREN))
            {
                // multiple bind
                PARSE_RULE(dummy, ParseDefines(expr));
            }
            else
            {
                // single bind
                PARSE_RULE(dummy, ParseDefine(expr));
            }

            return expr;
        }
        
        return target;
    }
    
    Ref<Expr> FinchParser::Assignment()
    {
        if (LookAhead(TOKEN_NAME, TOKEN_ARROW))
        {
            String name = Consume().Text();
            
            Consume(); // the arrow
            
            // handle assigning the special "undefined" value
            if (Match(TOKEN_UNDEFINED))
            {
                return Ref<Expr>(new UndefineExpr(name));
            }
            else
            {
                PARSE_RULE(value, Assignment());
                return Ref<Expr>(new VarExpr(name, value));
            }
        }
        else if (LookAhead(TOKEN_NAME, TOKEN_LONG_ARROW))
        {
            String name = Consume().Text();
            
            Consume(); // the arrow
            
            // get the initial value
            PARSE_RULE(value, Assignment());
            
            return Ref<Expr>(new SetExpr(name, value));
        }
        else return Cascade();
    }
    
    Ref<Expr> FinchParser::Cascade()
    {
        bool isMessage = false;
        Ref<Expr> keyword = Keyword(isMessage);
        
        // if we got an actual message send, we can cascade it.
        if (isMessage)
        {
            MessageExpr * expr = static_cast<MessageExpr*>(&*keyword);
            
            while (Match(TOKEN_COMMA))
            {
                Array<Ref<Expr> > args;
                bool dummy;
                
                //### bob: there's overlap here with Keyword(), Operator(), and
                // Unary(). should refactor.
                // figure out what kind of method we're cascading
                if (LookAhead(TOKEN_NAME))
                {
                    // unary
                    String name = Consume().Text();
                    expr->AddSend(name, args);
                }
                else if (LookAhead(TOKEN_OPERATOR))
                {
                    // binary
                    String name = Consume().Text();
                    
                    // one arg
                    args.Add(Unary(dummy));
                    expr->AddSend(name, args);
                }
                else if (LookAhead(TOKEN_KEYWORD))
                {
                    // keyword
                    String name;
                    while (LookAhead(TOKEN_KEYWORD))
                    {
                        // build the full method name
                        name += Consume().Text();
                        
                        // parse each keyword's arg
                        args.Add(Operator(dummy));
                    }
                    
                    expr->AddSend(name, args);
                }
            }
        }
        
        return keyword;
    }
    
    Ref<Expr> FinchParser::Keyword(bool & isMessage)
    {
        PARSE_RULE(object, Operator(isMessage));
        
        Ref<Expr> keyword = ParseKeyword(object);
        if (!keyword.IsNull())
        {
            isMessage = true;
            return keyword;
        }
        
        return object;
    }
    
    Ref<Expr> FinchParser::Operator(bool & isMessage)
    {
        PARSE_RULE(object, Unary(isMessage));
        
        while (LookAhead(TOKEN_OPERATOR))
        {
            String op = Consume().Text();
            PARSE_RULE(arg, Unary(isMessage));

            Array<Ref<Expr> > args;
            args.Add(arg);
            
            isMessage = true;
            object = Ref<Expr>(new MessageExpr(object, op, args));
        }
        
        return object;
    }
    
    Ref<Expr> FinchParser::Unary(bool & isMessage)
    {
        PARSE_RULE(object, Primary());
        
        while (LookAhead(TOKEN_NAME))
        {
            String message = Consume().Text();
            Array<Ref<Expr> > args;
            
            isMessage = true;
            object = Ref<Expr>(new MessageExpr(object, message, args));
        }
        
        return object;
    }
    
    Ref<Expr> FinchParser::Primary()
    {
        if (LookAhead(TOKEN_NAME))
        {
            return Ref<Expr>(new NameExpr(Consume().Text()));
        }
        else if (LookAhead(TOKEN_NUMBER))
        {
            return Ref<Expr>(new NumberExpr(Consume().Number()));
        }
        else if (LookAhead(TOKEN_STRING))
        {
            return Ref<Expr>(new StringExpr(Consume().Text()));
        }
        else if (LookAhead(TOKEN_KEYWORD))
        {
            // implicit receiver keyword message
            return ParseKeyword(Ref<Expr>(new NameExpr("Ether")));
        }
        //### getting rid of this for now to possibly free it up for some other
        // use
        /*
        else if (Match(TOKEN_DOT))
        {
            return Ref<Expr>(new NameExpr("self"));
        }*/
        else if (Match(TOKEN_SELF))
        {
            return Ref<Expr>(new SelfExpr());
        }
        else if (Match(TOKEN_LEFT_PAREN))
        {
            if (Match(TOKEN_PIPE))
            {
                // object literal

                // parse the parent
                PARSE_RULE(parent, Assignment());
                CONSUME(TOKEN_PIPE, "Expect closing '|' after parent.");
                Ref<Expr> object = Ref<Expr>(new ObjectExpr(parent));
                
                PARSE_RULE(dummy, ParseDefines(object));

                return object;
            }
            else
            {
                // just a parenthesized expression
                PARSE_RULE(expression, Expression());
                CONSUME(TOKEN_RIGHT_PAREN, "Expect closing ')'.");
                return expression;
            }
        }
        else if (Match(TOKEN_LEFT_BRACKET))
        {
            Array<Ref<Expr> > expressions;
            
            // allow zero-element arrays
            if (!LookAhead(TOKEN_RIGHT_BRACKET))
            {
                PARSE_RULE(dummy, ParseSequence(expressions));
            }
            
            CONSUME(TOKEN_RIGHT_BRACKET, "Expect closing ']'.");
            
            return Ref<Expr>(new ArrayExpr(expressions));
        }
        else if (Match(TOKEN_LEFT_BRACE))
        {
            Array<String> args;
            
            // see if there are args
            if (Match(TOKEN_PIPE))
            {
                while (LookAhead(TOKEN_NAME))
                {
                    args.Add(Consume().Text());
                }
                
                CONSUME(TOKEN_PIPE, "Expect closing '|' after block arguments.");
                
                // if there were no named args, but there were pipes (||),
                // use an automatic "it" arg
                if (args.Count() == 0) args.Add("it");
            }
            
            PARSE_RULE(body, Expression());
            CONSUME(TOKEN_RIGHT_BRACE, "Expect closing '}' after block.");
            
            return Ref<Expr>(new BlockExpr(args, body));
        }
        else return ParseError("Unexpected token.");
    }
    
    Ref<Expr> FinchParser::ParseSequence(Array<Ref<Expr> > & expressions)
    {
        PARSE_RULE(expression, Bind());
        expressions.Add(expression);
        
        while (Match(TOKEN_LINE))
        {
            // there may be a trailing line after the last expression in a
            // block. if we eat the line and then see a closing brace or eof,
            // just stop here.
            if (LookAhead(TOKEN_RIGHT_PAREN)) break;
            if (LookAhead(TOKEN_RIGHT_BRACKET)) break;
            if (LookAhead(TOKEN_RIGHT_BRACE)) break;
            if (LookAhead(TOKEN_EOF)) break;
            
            PARSE_RULE(next, Bind());
            expressions.Add(next);
        }
        
        // just return a random non-null expression to show success
        return expressions[0];
    }

    // Parses just the message send part of a keyword message: "foo: a bar: b"
    Ref<Expr> FinchParser::ParseKeyword(Ref<Expr> object)
    {
        String             message;
        Array<Ref<Expr> >  args;
        
        while (LookAhead(TOKEN_KEYWORD))
        {
            message += Consume().Text();
            
            bool dummy;
            PARSE_RULE(arg, Operator(dummy));
            args.Add(arg);
        }
        
        if (message.Length() > 0)
        {
            return Ref<Expr>(new MessageExpr(object, message, args));
        }
        
        return Ref<Expr>();
    }
    
    Ref<Expr> FinchParser::ParseDefines(Ref<Expr> expr)
    {
        do
        {
            PARSE_RULE(dummy, ParseDefine(expr));
            
            CONSUME(TOKEN_LINE, "Definitions must be separated by lines.");
        } while (!Match(TOKEN_RIGHT_PAREN));
        
        return expr;
    }
    
    Ref<Expr> FinchParser::ParseDefine(Ref<Expr> expr)
    {
        Array<String> args;
        
        // figure out what kind of method we're binding
        if (LookAhead(TOKEN_NAME))
        {
            // unary
            String name = Consume().Text();
            
            PARSE_RULE(dummy, ParseDefineBody(expr, name, args));
        }
        else if (LookAhead(TOKEN_OPERATOR))
        {
            // binary
            String name = Consume().Text();
            
            // one arg
            EXPECT(TOKEN_NAME, "Expect name after operator in a bind expression.");
            args.Add(Consume().Text());
            
            PARSE_RULE(dummy, ParseDefineBody(expr, name, args));
        }
        else if (LookAhead(TOKEN_KEYWORD))
        {
            // keyword
            String name;
            while (LookAhead(TOKEN_KEYWORD))
            {
                // build the full method name
                name += Consume().Text();
                
                // parse each keyword's arg
                EXPECT(TOKEN_NAME, "Expect name after keyword in a bind expression.");
                args.Add(Consume().Text());
            }
            
            PARSE_RULE(dummy, ParseDefineBody(expr, name, args));
        }
        else return ParseError("Expect message after '::'.");
        
        return expr;
    }
    
    Ref<Expr> FinchParser::ParseDefineBody(Ref<Expr> expr, String name,
                                         const Array<String> & args)
    {
        // parse the block
        CONSUME(TOKEN_LEFT_BRACE, "Expect '{' to begin bound block.");
        PARSE_RULE(body, Expression());
        CONSUME(TOKEN_RIGHT_BRACE, "Expect '}' to close block.");
        
        // attach the block's arguments
        Ref<Expr> block = Ref<Expr>(new BlockExpr(args, body));
        
        // desugar to the basic addMethod:body: form
        Array<Ref<Expr> > addMethodArgs;
        addMethodArgs.Add(Ref<Expr>(new StringExpr(name)));
        addMethodArgs.Add(block);
        
        DefineExpr * define = static_cast<DefineExpr*>(&*expr);
        define->Define(true, name, block);
        
        return expr;
    }
    
    Ref<Expr> FinchParser::ParseError(const char * message)
    {
        //### bob: using stringstream here is gross
        std::stringstream error;
        error << "Parse error on " << Current() << ": " << message;
        mErrorReporter.Error(String(error.str().c_str()));

        return Ref<Expr>();
    }

}

