#include "calculator_tool.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

class ExpressionParser {
public:
    explicit ExpressionParser(std::string expression)
        : expression_(std::move(expression)), position_(0) {}

    double parse() {
        const double value = parse_expression();
        skip_whitespace();
        if (position_ != expression_.size()) {
            throw std::runtime_error("Unexpected character at position " +
                                     std::to_string(position_));
        }
        return value;
    }

private:
    std::string expression_;
    std::size_t position_;

    double parse_expression() {
        double value = parse_term();
        while (true) {
            skip_whitespace();
            if (match('+')) {
                value += parse_term();
            } else if (match('-')) {
                value -= parse_term();
            } else {
                return value;
            }
        }
    }

    double parse_term() {
        double value = parse_factor();
        while (true) {
            skip_whitespace();
            if (match('*')) {
                value *= parse_factor();
            } else if (match('/')) {
                const double divisor = parse_factor();
                if (divisor == 0.0) {
                    throw std::runtime_error("Division by zero");
                }
                value /= divisor;
            } else {
                return value;
            }
        }
    }

    double parse_factor() {
        skip_whitespace();

        if (match('+')) {
            return parse_factor();
        }
        if (match('-')) {
            return -parse_factor();
        }
        if (match('(')) {
            const double value = parse_expression();
            skip_whitespace();
            if (!match(')')) {
                throw std::runtime_error("Missing closing parenthesis");
            }
            return value;
        }

        return parse_number();
    }

    double parse_number() {
        skip_whitespace();
        const std::size_t start = position_;

        bool seen_digit = false;
        while (position_ < expression_.size()) {
            const char current = expression_[position_];
            if (std::isdigit(static_cast<unsigned char>(current)) || current == '.') {
                seen_digit = true;
                ++position_;
            } else {
                break;
            }
        }

        if (!seen_digit) {
            throw std::runtime_error("Expected number at position " +
                                     std::to_string(start));
        }

        return std::stod(expression_.substr(start, position_ - start));
    }

    void skip_whitespace() {
        while (position_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[position_]))) {
            ++position_;
        }
    }

    bool match(char expected) {
        if (position_ < expression_.size() && expression_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }
};

}  // namespace

std::string CalculatorTool::name() const {
    return "calculator";
}

std::string CalculatorTool::description() const {
    return "Evaluate arithmetic expressions with +, -, *, / and parentheses.";
}

ToolResult CalculatorTool::run(const std::string& args) const {
    try {
        const double value = ExpressionParser(args).parse();
        std::ostringstream output;
        output << std::setprecision(12) << value;
        return {true, output.str()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}
