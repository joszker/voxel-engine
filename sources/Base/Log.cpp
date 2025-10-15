#include "Log.h"

#include <regex>
#include <iostream>
#include <fstream>

#if _WIN32
#include <Windows.h>
#endif

// Low intensity foreground
#define ANSI_FG_LI_BLK "\x1b[0;30m"
#define ANSI_FG_LI_RED "\x1b[0;31m"
#define ANSI_FG_LI_GRN "\x1b[0;32m"
#define ANSI_FG_LI_YEL "\x1b[0;33m"
#define ANSI_FG_LI_BLU "\x1b[0;34m"
#define ANSI_FG_LI_MAG "\x1b[0;35m"
#define ANSI_FG_LI_CYN "\x1b[0;36m"
#define ANSI_FG_LI_WHT "\x1b[0;37m"

// High intensity foreground
#define ANSI_FG_HI_BLK "\x1b[0;90m"
#define ANSI_FG_HI_RED "\x1b[0;91m"
#define ANSI_FG_HI_GRN "\x1b[0;92m"
#define ANSI_FG_HI_YEL "\x1b[0;93m"
#define ANSI_FG_HI_BLU "\x1b[0;94m"
#define ANSI_FG_HI_MAG "\x1b[0;95m"
#define ANSI_FG_HI_CYN "\x1b[0;96m"
#define ANSI_FG_HI_WHT "\x1b[0;97m"

//Low intensity background
#define ANSI_BG_LI_BLK "\x1b[40m"
#define ANSI_BG_LI_RED "\x1b[41m"
#define ANSI_BG_LI_GRN "\x1b[42m"
#define ANSI_BG_LI_YEL "\x1b[43m"
#define ANSI_BG_LI_BLU "\x1b[44m"
#define ANSI_BG_LI_MAG "\x1b[45m"
#define ANSI_BG_LI_CYN "\x1b[46m"
#define ANSI_BG_LI_WHT "\x1b[47m"

// High intensity background
#define ANSI_BG_HI_BLK "\x1b[0;100m"
#define ANSI_BG_HI_RED "\x1b[0;101m"
#define ANSI_BG_HI_GRN "\x1b[0;102m"
#define ANSI_BG_HI_YEL "\x1b[0;103m"
#define ANSI_BG_HI_BLU "\x1b[0;104m"
#define ANSI_BG_HI_MAG "\x1b[0;105m"
#define ANSI_BG_HI_CYN "\x1b[0;106m"
#define ANSI_BG_HI_WHT "\x1b[0;107m"

#define ANSI_RESET     "\x1b[0m"

namespace Log
{

static const char *LOG_LEVEL_SMALL[] = {
  "trace", "info", "warn", "error", "fatal"
};

static const char *LOG_LEVEL_CAPS[] = {
  "TRACE", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *LOG_LEVEL_ANSI_COLORS[] = {
  "\x1b[94m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[38;2;255;0;0;48;2;64;0;0m"
};


#if defined(_WIN32) || defined(__CYGWIN__)
constexpr char LOG_PATH_SEP = '\\';
#else
constexpr char LOG_PATH_SEP = '/';
#endif


bool Replace(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if(start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}


Logger::Logger() : m_level(0)
{
    m_message.reserve(128);
    m_buffer.reserve(128);
}

void Logger::Emit(int level, std::string filePath, unsigned line)
{
    if (level < m_level)
        return;

    std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);

    std::time_t timeNow = std::time(nullptr);
    std::tm dateTime = *std::localtime(&timeNow);
    char timeStr[32];

    for (Sink& sink : m_sinks)
    {
        if (level >= sink.level)
        {
            std::strftime(timeStr, sizeof(timeStr), sink.tmfmt.data(), &dateTime);

            m_buffer.assign(sink.fmt);
            Replace(m_buffer, "%%", "%");
            Replace(m_buffer, "%T", std::string(timeStr));
            Replace(m_buffer, "%L", LOG_LEVEL_CAPS[level]);
            Replace(m_buffer, "%l", LOG_LEVEL_SMALL[level]);
            Replace(m_buffer, "%F", filePath);
            Replace(m_buffer, "%f", fileName);
            Replace(m_buffer, "%#", std::to_string(line));
            Replace(m_buffer, "%^", LOG_LEVEL_ANSI_COLORS[level]);
            Replace(m_buffer, "%$", ANSI_RESET);
            Replace(m_buffer, "%?", m_message);
            
            *sink.stream << m_buffer << "\n";

            if (sink.flush)
                sink.stream->flush();
        }
    }
}

void Logger::SetLevel(int level)
{
    m_level = level;
}

void Logger::AddSink(const Sink& sink)
{
    m_sinks.emplace_back(sink);
}


void Init()
{
#if _WIN32
    /* Enable virtual terminal sequences on Windows */
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole)
    {
        DWORD consoleMode;
        GetConsoleMode(hConsole, &consoleMode);
        SetConsoleMode(hConsole, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

Logger& GetMainLogger()
{
    static bool s_initialized = false;
    static Logger s_logger;

    if (!s_initialized)
    {
        Log::Sink console;
        console.stream = &std::cout;
        console.tmfmt = "%H:%M:%S";
        console.fmt = "[%T] %^%?%$";
        console.flush = true;
        /*
        static std::ofstream fd("log.txt");
        Log::Sink file;
        file.stream = &fd;
        file.tmfmt = "%H:%M:%S";
        file.fmt = "[%T][%L] %?";
        s_logger.AddSink(file);
        */     
        s_logger.AddSink(console);
        s_initialized = true;
    }
    return s_logger;
}

}
