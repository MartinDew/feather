FetchContent_Declare(args
    GIT_REPOSITORY https://github.com/Taywee/args.git
    GIT_TAG 6.4.7
)

list(APPEND THIRDPARTY_PACKAGES args)
list(APPEND THIRDPARTIES taywee::args)

