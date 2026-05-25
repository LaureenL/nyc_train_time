#pragma once

#include <time.h>

#include "train_arrivals.h"

void setupDisplay();
void renderArrivalsToDisplay(const struct DecodeStats &stats);
void refreshHeaderTimePartial(time_t currentTime);
