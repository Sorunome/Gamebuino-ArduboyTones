/**
 * @file ArduboyTones.cpp
 * \brief An Arduino library for playing tones and tone sequences, 
 * intended for the Arduboy game system.
 */

/*****************************************************************************
  ArduboyTones

An Arduino library to play tones and tone sequences.

Specifically written for use by the Arduboy miniature game system
https://www.arduboy.com/
but could work with other Arduino AVR boards that have 16 bit timer 3
available, by changing the port and bit definintions for the pin(s)
if necessary.

Copyright (c) 2017 Scott Allen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*****************************************************************************/

#include "Gamebuino-ArduboyTones.h"
#include <Gamebuino-Meta.h>

// pointer to a function that indicates if sound is enabled
static bool (*outputEnabled)();

static volatile long durationToggleCount = 0;
static volatile bool tonesPlaying = false;
static volatile bool toneSilent;
#ifdef TONES_VOLUME_CONTROL
static volatile bool toneHighVol;
static volatile bool forceHighVol = false;
static volatile bool forceNormVol = false;
#endif

static volatile uint16_t *tonesStart;
static volatile uint16_t *tonesIndex;
static volatile uint16_t toneSequence[MAX_TONES * 2 + 1];

Gamebuino_Meta::Sound_Channel* gb_channel = 0;

class GB_Sound_Handler;
GB_Sound_Handler* gb_handler = 0;

void gb_updateTones();

class GB_Sound_Handler : public Gamebuino_Meta::Sound_Handler {
public:
  GB_Sound_Handler() : Gamebuino_Meta::Sound_Handler(0) {
    
  };
  ~GB_Sound_Handler() {
    gb_channel = 0;
    gb_handler = 0;
  };
  void update() {
    gb_updateTones();
  }
  void rewind() {
    // do nothing, this is a dummy
  }
  void escapseChannel() {
    gb_channel = channel;
  }
};



ArduboyTones::ArduboyTones(boolean (*outEn)())
{
  outputEnabled = outEn;

  toneSequence[MAX_TONES * 2] = TONES_END;
  // trash
}

void ArduboyTones::tone(uint16_t freq, uint16_t dur)
{
  tonesStart = tonesIndex = toneSequence; // set to start of sequence array
  toneSequence[0] = freq;
  toneSequence[1] = dur;
  toneSequence[2] = TONES_END; // set end marker
  nextTone(); // start playing
}

void ArduboyTones::tone(uint16_t freq1, uint16_t dur1,
                        uint16_t freq2, uint16_t dur2)
{
  tonesStart = tonesIndex = toneSequence; // set to start of sequence array
  toneSequence[0] = freq1;
  toneSequence[1] = dur1;
  toneSequence[2] = freq2;
  toneSequence[3] = dur2;
  toneSequence[4] = TONES_END; // set end marker
  nextTone(); // start playing
}

void ArduboyTones::tone(uint16_t freq1, uint16_t dur1,
                        uint16_t freq2, uint16_t dur2,
                        uint16_t freq3, uint16_t dur3)
{
  tonesStart = tonesIndex = toneSequence; // set to start of sequence array
  toneSequence[0] = freq1;
  toneSequence[1] = dur1;
  toneSequence[2] = freq2;
  toneSequence[3] = dur2;
  toneSequence[4] = freq3;
  toneSequence[5] = dur3;
  // end marker was set in the constructor and will never change
  nextTone(); // start playing
}

void ArduboyTones::tones(const uint16_t *tones)
{
  tonesStart = tonesIndex = (uint16_t *)tones; // set to start of sequence array
  nextTone(); // start playing
}

void ArduboyTones::tonesInRAM(uint16_t *tones)
{
  tonesStart = tonesIndex = tones; // set to start of sequence array
  nextTone(); // start playing
}

void ArduboyTones::noTone()
{
  if (gb_channel) {
    gb_channel->use = false;
  }
  tonesPlaying = false;
}

void ArduboyTones::volumeMode(uint8_t mode)
{
#ifdef TONES_VOLUME_CONTROL
  forceNormVol = false; // assume volume is tone controlled
  forceHighVol = false;

  if (mode == VOLUME_ALWAYS_NORMAL) {
    forceNormVol = true;
  }
  else if (mode == VOLUME_ALWAYS_HIGH) {
    forceHighVol = true;
  }
#endif
}

bool ArduboyTones::playing()
{
  return tonesPlaying;
}

int32_t gb_tone_duration = 0;

void ArduboyTones::nextTone()
{
  uint16_t freq = getNext();
  if (freq == TONES_END) {
    noTone();
    return;
  }
  tonesPlaying = true;
  if (freq == TONES_REPEAT) { // if frequency is actually a "repeat" marker
    tonesIndex = tonesStart; // reset to start of sequence
    freq = getNext();
  }
  toneHighVol = true;
#ifdef TONES_VOLUME_CONTROL
  if (((freq & TONE_HIGH_VOLUME) || forceHighVol) && !forceNormVol) {
    toneHighVol = true;
  }
  else {
    toneHighVol = false;
  }
#endif
  
  freq &= ~TONE_HIGH_VOLUME; // strip volume indicator from frequency
  
  if (freq == 0) {
    // silent tone
    toneSilent = true;
  } else {
    toneSilent = false;
  }
  
  if (!outputEnabled()) { // if sound has been muted
    toneSilent = true;
  }
  
  uint16_t dur = getNext();
  
  if (!gb_channel) {
    gb_handler = new GB_Sound_Handler();
    gb.sound.play(gb_handler, true);
    gb_handler->escapseChannel();
  }
  
  if (!gb_channel) {
    delete gb_handler;
    gb_handler = 0;
    return;
  }
  
  gb_channel->type = Gamebuino_Meta::Sound_Channel_Type::pattern;
  gb_channel->total = 22050 / freq;
  gb_channel->index = 0;
  if (toneSilent) {
    gb_channel->amplitude = 0;
  } else if (toneHighVol) {
    gb_channel->amplitude = 0x30;
  } else {
    gb_channel->amplitude = 12;
  }
  
  gb_channel->use = true;
  
  gb_tone_duration = dur;
  
}

uint16_t ArduboyTones::getNext()
{
  return *tonesIndex++;
}

void gb_updateTones() {
  if (!gb_channel) {
    ArduboyTones::noTone();
    return;
  }
  gb_tone_duration -= gb.getTimePerFrame();
  if (gb_tone_duration <= 0) {
    ArduboyTones::nextTone();
  }
}
