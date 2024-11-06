#pragma once

#include <fmt/format.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <stdio.h>

class MyFile {
public:
    MyFile(const std::string &filename, const std::string &mode) : filename(filename) {
        fp = fopen(filename.c_str(), mode.c_str());
        if (!fp) {
            throw std::runtime_error(fmt::format("failed to open file", filename));
        }
    }

    ~MyFile() {
        fclose(fp);
    }

    void Write(const char *data, int size) {
        int wroteNum = fwrite(data, size, 1, fp);
        if (wroteNum != 1) {
            throw std::runtime_error(fmt::format("failed to write file", filename));
        }
    }

    void Write(const unsigned char *data, int size) {
        Write(reinterpret_cast<const char *>(data), size);
    }

    void Write(const std::vector<char> &buf) {
        Write(buf.data(), buf.size());
    }

    template <std::size_t N>
    void Write(const std::array<char, N> &buf) {
        Write(buf.data(), buf.size());
    }

private:
    FILE *fp;
    std::string filename;
};