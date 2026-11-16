# SPDX-License-Identifier: MIT
# The host's own words, in the language the device is set to. Pure: it
# draws nothing and knows no screen; graphics, the kit and the services
# take their words from here.
idf_component_register(SRCS "words.c"
                       INCLUDE_DIRS "include")
