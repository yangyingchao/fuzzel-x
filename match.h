#pragma once

#include <unistd.h>
#include "application.h"

struct match {
    const struct application *application;
    ssize_t start_title;
    ssize_t start_comment;
};
