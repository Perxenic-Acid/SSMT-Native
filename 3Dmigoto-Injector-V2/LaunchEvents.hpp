#pragma once

#include <windows.h>

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

class LaunchEventEmitter
{
public:
    explicit LaunchEventEmitter(bool enabled)
        : enabled_(enabled)
        , output_(GetStdHandle(STD_OUTPUT_HANDLE))
    {
    }

    LaunchEventEmitter(bool enabled, HANDLE output)
        : enabled_(enabled)
        , output_(output)
    {
    }

    bool enabled() const
    {
        return enabled_;
    }

    void emit(const char *event)
    {
        if (!enabled_)
            return;

        write(std::string("{\"event\":\"") + escape(event) + "\"}\n");
    }

    void emit_pid(const char *event, DWORD pid)
    {
        if (!enabled_)
            return;

        write(std::string("{\"event\":\"") + escape(event) + "\",\"pid\":" +
              std::to_string(static_cast<unsigned long>(pid)) + "}\n");
    }

    void emit_module(const char *event, const wchar_t *module)
    {
        if (!enabled_)
            return;

        const std::string utf8 = to_utf8(module);
        write(std::string("{\"event\":\"") + escape(event) +
              "\",\"module\":\"" + escape(utf8) + "\"}\n");
    }

    void emit_error(const char *stage, const char *code, const char *message)
    {
        if (!enabled_)
            return;

        write(std::string("{\"event\":\"launch_error\",\"stage\":\"") +
              escape(stage) + "\",\"code\":\"" + escape(code) +
              "\",\"message\":\"" + escape(message) + "\"}\n");
    }

private:
    bool enabled_;
    HANDLE output_;

    void write(const std::string &line) const
    {
        if (!output_ || output_ == INVALID_HANDLE_VALUE)
            return;

        DWORD written = 0;
        WriteFile(output_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }

    static std::string escape(const std::string &value)
    {
        std::ostringstream output;
        output << std::uppercase << std::hex;
        for (unsigned char character : value)
        {
            switch (character)
            {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20)
                {
                    output << "\\u" << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character);
                }
                else
                {
                    output << character;
                }
                break;
            }
        }
        return output.str();
    }

    static std::string to_utf8(const wchar_t *value)
    {
        if (!value || !*value)
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
            return {};

        std::string result(static_cast<size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                            result.data(), required, nullptr, nullptr);
        if (!result.empty() && result.back() == '\0')
            result.pop_back();
        return result;
    }
};
