#ifndef SHELL_H
#define SHELL_H

#include <string>
#include <vector>

void* shell_malloc(size_t s);
void shell_free(void* p);
size_t shell_alloc_count();

std::vector<std::string> tokenize(const std::string& s);

#endif
