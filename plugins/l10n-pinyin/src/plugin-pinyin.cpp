extern "C" {
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

const char* l10n_plugin_init(const char *path);
const char *l10n_plugin_translate(const char *input);
}

#include <algorithm>
#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/Pinyin.h>
#include <cstring>
#include <filesystem>
#include <iostream>

std::unique_ptr<Pinyin::Pinyin> instance;

const char* l10n_plugin_init(const char *path_) {
    std::filesystem::path path = path_;
    path = path.parent_path() / "dict";
    if (!exists (path)) {
        static std::string ret = "failed to find dictionary from path: ";
        ret += path;
        return ret.c_str();
    }

    Pinyin::setDictionaryPath(path);
    instance = std::make_unique<Pinyin::Pinyin>();

    return nullptr;
}

const char *l10n_plugin_translate(const char *input) {
    if (std::all_of(input, input + strlen(input),
                    [](char c) { return static_cast<unsigned char>(c) <= 127; })) {
        return input;
    }

    static std::string res;
    res = instance->hanziToPinyin(input, Pinyin::ManTone::Style::NORMAL, Pinyin::Error::Default, false, true)
              .toStdStr();

    return res.c_str();
}
