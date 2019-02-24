#pragma once

#include <stddef.h>
#include "application.h"

struct match {
    const struct application *application;
    size_t start;
};
