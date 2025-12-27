#pragma once

#include <string>
#include <vector>

class Tokenizer
{
public:
  std::vector<std::string> tokenize(const std::string &line) const;

private:
  enum class Mode
  {
    None,
    Single,
    Double
  };

  struct TokenState
  {
    std::vector<std::string> parts{};
    std::string currentToken{};
    bool tokenStarted{false};
    Mode mode{Mode::None};
  };

  struct Cursor
  {
    const std::string &line;
    std::size_t index{0};

    bool atEnd() const
    {
      return index >= line.size();
    }

    char current() const
    {
      return line[index];
    }

    bool hasNext() const
    {
      return index + 1 < line.size();
    }

    char next() const
    {
      return line[index + 1];
    }

    void advance()
    {
      ++index;
    }
  };

  void pushToken(TokenState &state) const;
  void handleSingle(TokenState &state, Cursor &cursor) const;
  void handleDouble(TokenState &state, Cursor &cursor) const;
  void handleNone(TokenState &state, Cursor &cursor) const;
};
