#pragma once
#include "Base.h"
#include "Base/Log.h"
#include <optional>
#include <vector>
#include <string_view>
#include <fstream>
#include <filesystem>


/* File system realted utilities */
namespace FileSystem
{
    /* Reads the entire file into memory */
    inline Opt<std::vector<byte>> ReadFile(std::string_view path)
    {
        /* Open file stream with the stream position at the end */
        std::ifstream fs(path.data(), std::ios::ate | std::ios::binary);

        if (!fs.is_open())
        {
            LOG_ERROR("Failed to open file: '{}'", strerror(errno));
            return {};
        }

        /* Get file size */
        int64_t size = fs.tellg();
        if (size < 0)
        {
            LOG_ERROR("tellg() failed: '{}'", strerror(errno));
            return {};
        }
        if (size == 0)
            return std::vector<byte>();

        /* Seek to the beginning */
        if (!fs.seekg(0))
        {
            LOG_ERROR("seekg() failed: '{}'", strerror(errno));
            return {};
        }

        /* Read entire file */
        std::vector<byte> data(size);
        if (!fs.read((char*)&data[0], size))
        {
            LOG_ERROR("Failed to read {} bytes: '{}'", size, strerror(errno));
            return {};
        }
        return std::move(data);
    }


    /* Returns whether file or directory exists/is accessible */
    inline bool Exists(std::string_view path)
    {
        return std::filesystem::exists(path);
    }
}
