// app/quotes.h — recueil de citations philosophiques au demarrage.
#pragma once

#include <cstddef>

namespace gw::app {

struct Quote {
    const char* text;
    const char* author;
};

std::size_t quote_count();
const Quote& get_quote(std::size_t index);
const Quote& get_quote_of_the_day();
const Quote& get_random_quote();

} // namespace gw::app
