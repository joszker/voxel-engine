#pragma once
#include "Base.h"

#include <ctime>
#include <string>
#include <vector>
#include <format>
#include <iterator>
#include <ostream>
#include <mutex>


#define LOG_LEVEL_TRACE     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_WARNING   2
#define LOG_LEVEL_ERROR     3
#define LOG_LEVEL_FATAL     4



namespace Log 
{
    
struct Sink
{
    std::ostream* stream{};
    std::string_view tmfmt = "%H:%M:%S";
    std::string_view fmt = "[%T] %?";
    int level = 0;
    bool flush = false;
};


class Logger
{
public:
    Logger();

    void SetLevel(int level);
    void AddSink(const Sink& sink);

    template <typename... Types>
    inline void Message(int level, const char *file, unsigned line, std::string_view fmt, Types&&... args)
    {
        m_message.clear();
        std::vformat_to(std::back_inserter(m_message), fmt, std::make_format_args(args...));
        Emit(level, file, line);
    }

private:
    void Emit(int level, std::string filePath, unsigned line);

private:
    int m_level;
    std::string m_message;
    std::string m_buffer;
    std::vector<Sink> m_sinks;
};


void Init();
Logger& GetMainLogger();
}


#ifndef LOG_LEVEL
    #define LOG_LEVEL 0
#endif

#define LOG_GENERIC(level, ...) Log::GetMainLogger().Message(level, __FILE__, __LINE__, __VA_ARGS__)

#if LOG_LEVEL <= LOG_LEVEL_TRACE
    #define LOG_TRACE(...)      Log::GetMainLogger().Message(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
    #define LOG_INFO(...)       Log::GetMainLogger().Message(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARNING
    #define LOG_WARNING(...)    Log::GetMainLogger().Message(LOG_LEVEL_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
    #define LOG_ERROR(...)      Log::GetMainLogger().Message(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#endif

#if LOG_LEVEL <= LOG_LEVEL_FATAL
    #define LOG_FATAL(...)      Log::GetMainLogger().Message(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#endif