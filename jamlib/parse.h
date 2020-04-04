#pragma once

#include <string>
#include <variant>
#include <vector>

namespace jamlib
  {

  struct token
    {
    enum e_type
      {
      T_BAD,
      T_NUMBER,
      T_DOT,
      T_PLUS,
      T_DOLLAR,
      T_MINUS,
      T_COMMAND,
      T_FILENAME,
      T_DELIMITER_SLASH,
      T_COMMA,
      T_HASHTAG,
      T_TEXT,
      T_EXTERNAL_COMMAND
      };

    e_type type;
    std::string value;

    token(e_type i_type, const std::string& v) : type(i_type), value(v) {}
    };

  std::vector<token> tokenize(const std::string& str);

  struct LineNumber;
  struct CharacterNumber;
  struct Dot;
  struct EndOfFile;
  struct RegExp;

  struct LineNumber
    {
    uint64_t value;
    };

  struct CharacterNumber
    {
    uint64_t value;
    };

  struct Dot
    {
    };

  struct EndOfFile
    {
    };

  struct RegExp
    {
    std::string regexp;
    };

  typedef std::variant<CharacterNumber, Dot, EndOfFile, LineNumber, RegExp> SimpleAddress;

  template<typename T>
  class Precedence { public: std::vector<T> operands; std::vector<std::string> fops; };

  typedef Precedence<SimpleAddress> AddressTerm;
  typedef Precedence<AddressTerm> AddressRange;

  struct Text
    {
    std::string text;
    };

  struct Cmd_external
    {
    std::string command;
    };

  struct Cmd_external_input
    {
    std::string command;
    };

  struct Cmd_external_output
    {
    std::string command;
    };

  struct Cmd_external_io
    {
    std::string command;
    };

  struct Cmd_a
    {
    Text txt;
    };

  struct Cmd_b
    {
    uint64_t value;
    };

  struct Cmd_c
    {
    Text txt;
    };

  struct Cmd_i
    {
    Text txt;
    };

  struct Cmd_l
    {
    std::string filename;
    };

  struct Cmd_d
    {
    };

  struct Cmd_s
    {
    Text txt;
    RegExp regexp;
    };

  struct Cmd_m
    {
    AddressRange addr;
    };

  struct Cmd_t
    {
    AddressRange addr;
    };

  struct Cmd_p
    {
    };

  struct Cmd_p_dot
    {
    };

  struct Cmd_q
    {
    };

  struct Cmd_null
    {
    };

  struct Cmd_R
    {
    uint64_t value;
    };

  struct Cmd_u
    {
    uint64_t value;
    };

  struct Cmd_e
    {
    std::string filename;
    };

  struct Cmd_r
    {
    std::string filename;
    };

  struct Cmd_w
    {
    std::string filename;
    };

  struct Cmd_x;

  struct Cmd_g;

  struct Cmd_v;

  struct Cmd_A // ascii
    {
    };

  struct Cmd_U // UTF-8
    {
    };

  typedef std::variant<Cmd_external, Cmd_external_input, Cmd_external_output, Cmd_external_io, Cmd_a, Cmd_A, Cmd_b, Cmd_c, Cmd_d, Cmd_e, Cmd_g, Cmd_i, Cmd_l, Cmd_m, Cmd_null, Cmd_p, Cmd_p_dot, Cmd_q, Cmd_r, Cmd_R, Cmd_s, Cmd_t, Cmd_u, Cmd_U, Cmd_v, Cmd_w, Cmd_x> Command;

  struct Cmd_g
    {
    RegExp regexp;
    std::vector<Command> cmd;
    };

  struct Cmd_v
    {
    RegExp regexp;
    std::vector<Command> cmd;
    };

  struct Cmd_x
    {
    RegExp regexp;
    std::vector<Command> cmd;
    };

  typedef std::variant<AddressRange, Command> Expression;

  std::vector<Expression> parse(std::vector<token> tokens);

  }