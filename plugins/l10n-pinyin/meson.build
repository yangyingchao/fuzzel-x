l10n_pinyin_sources = [
    'cpp-pinyin/src/ChineseG2p.cpp',
    'cpp-pinyin/src/DictUtil.cpp',
    'cpp-pinyin/src/G2pglobal.cpp',
    'cpp-pinyin/src/Jyutping.cpp',
    'cpp-pinyin/src/Pinyin.cpp',
    'cpp-pinyin/src/PinyinRes.cpp',
    'cpp-pinyin/src/ToneConverter.cpp',
    'cpp-pinyin/src/U16Str.cpp',
    'cpp-pinyin/src/toneUtil/CanTone.cpp',
    'cpp-pinyin/src/toneUtil/ManTone.cpp',
    'cpp-pinyin/src/toneUtil/ManToneUtil.cpp',
    'src/plugin-pinyin.cpp',

]

l10n_pinyin_headers = [
    'cpp-pinyin/src/',
    'cpp-pinyin/src/toneUtil',
    'cpp-pinyin/include/',
]

l10n_pinyin = shared_library(
    'l10n-pinyin',
    l10n_pinyin_sources,
    include_directories: l10n_pinyin_headers,
    name_prefix: '',
    install: true,
    install_dir: join_paths(get_option('datadir'), 'fuzzel'),
)

install_subdir(
    'cpp-pinyin/res/dict',
    install_dir: join_paths(get_option('datadir'), 'fuzzel'),
)
