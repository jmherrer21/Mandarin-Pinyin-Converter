#pragma once
#include <string>
#include <vector>

// Extracts text from each page of the PDF at path.
// Returns one string per PDF page. Throws std::runtime_error on failure.
std::vector<std::string> extract_pages(const std::string& path);
