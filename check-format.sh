clang-format -style=file --dry-run -Werror `find src -regex '.*\.\(c\|cpp\|h\)'`
clang-format -style=file --dry-run -Werror `find examples -regex '.*\.\(c\|cpp\|h\)'`
