#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>

struct command {
    std::vector<std::string> args;
    std::string in_file;
    std::string out_file;
    bool append_out = false;
};

struct ParsedLine {
    std::vector<command> cmds;
    bool background = false;
};

ParsedLine parse_line(const std::string& line);

#endif