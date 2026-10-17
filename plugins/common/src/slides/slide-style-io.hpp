// A slide style in OBS's settings form: what the panels save, and what the source reads.
#pragma once

#include "slides/slide-style.hpp"

#include <obs.h>

namespace laycue::slides {

void writeStyle(obs_data_t *data, const SlideStyle &style);
SlideStyle readStyle(obs_data_t *data); // missing values keep SlideStyle's own defaults

} // namespace laycue::slides
