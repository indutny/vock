var vock = require('../vock'),
    util = require('util'),
    binding = require('bindings')('vock.node'),
    EventEmitter = require('events').EventEmitter,
    Buffer = require('buffer').Buffer;

var audio = exports;

//
// ### function Audio (rate)
// #### @rate {Number} Sample rate for input/output
// Creates wrapper for binding
//
function Audio(rate) {
  EventEmitter.call(this);

  this.audio = new binding.Audio(rate, rate / 25);
  this.opus = new binding.Opus(rate, 1);
  this.active = false;
  this.gain = 1;
  this.maxRms = 6000;

  this.shiftOffset = rate / 100;
  this.mixTimer = null;
  this.mixQueues = {};

  this._removeCallbacks();
};
util.inherits(Audio, EventEmitter);

//
// ### function create (rate)
// #### @rate {Number} Sample rate for input/output
// Wrapper for constructor
//
exports.create = function create(rate) {
  return new Audio(rate);
};

//
// ### function _removeCallbacks ()
// Internal only
//
Audio.prototype._removeCallbacks = function removeCallbacks() {
  this.audio.ondata = function() {};
};

//
// ### function computeGain (pcm)
// #### @pcm {Buffer} PCM buffer
// Computes desirable .gain value for pcm buffer
//
Audio.prototype.computeGain = function(pcm) {
  var rms = this.audio.getRms(pcm);
  if (rms > this.maxRms) {
    this.gain = this.maxRms / rms;
  } else {
    // Return gain back to normal
    this.gain = (1 + 20 * this.gain) / 21;
  }
};

//
// ### function start ()
// Start record/playback loop
//
Audio.prototype.start = function start() {
  var self = this;

  this.audio.ondata = self.ondata.bind(self);
  this.audio.start();
};

//
// ### function stop ()
// Stop record/playback loop
//
Audio.prototype.stop = function stop() {
  this.active = false;
  this._removeCallbacks();
  this.audio.stop();
};

//
// ### function ondata (pcm)
// #### @pcm {Buffer} PCM buffer
// Called when recorded some data from microphone
// (NOTE: pcm has fixed size there, rate/50 samples)
//
Audio.prototype.ondata = function ondata(pcm) {
  try {
    this.emit('data', this.opus.encode(pcm));
  } catch (e) {
    this.emit('error', e);
  }
};

//
// ### function play (pcm)
// #### @pcm {Buffer} PCM buffer
// Enqueue some PCM data for playback
//
Audio.prototype.play = function play(pcm) {
  try {
    if (pcm.length) {
      this.computeGain(pcm);
      this.audio.applyGain(pcm, this.gain);
    }
    this.audio.enqueue(pcm);
  } catch (e) {
    this.emit('error', e);
  }
};

Audio.prototype.mix = function write(channel, data) {
  var self = this,
      pcm = this.opus.decode(data ? data : null);

  if (pcm.length <= 0) return;

  if (!this.mixQueues[channel]) this.mixQueues[channel] = [];
  this.mixQueues[channel].push(pcm);

  // A timer is already hanging
  if (this.mixTimer) return;

  // Two frame sizes is a maximum latency
  this.scheduleShift();
};

Audio.prototype.shift = function shift() {
  var self = this,
      offset = this.shiftOffset,
      queues = this.mixQueues;

  var channels = Object.keys(queues).map(function(channel) {
    // Concat buffers in queue
    var buff = Buffer.concat(queues[channel]),
        head = buff.length > offset ? buff.slice(0, offset) : buff,
        tail = buff.length > offset ? buff.slice(offset) : null;

    queues[channel] = tail ? [tail] : [];

    return head;
  });

  // Mix channels and play result
  this.play(channels.reduce(function (a, b) {
    for (var i = 0; i < offset; i++) {
      if (a >= 0 && b >= 0 || a <=0 && b <= 0) {
        a[i] = a[i] + b[i] - (a[i] * b[i] / 32767);
      } else {
        a[i] = a[i] + b[i];
      }
    }
  }));

  // Setup new timer
  this.scheduleShift();
};

Audio.prototype.scheduleShift = function scheduleShift() {
  this.mixTimer = setTimeout(this.shift.bind(this),
                             1000 * this.shiftOffset / (2 * this.rate));
};
