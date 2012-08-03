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
// ### function play (data)
// #### @data {Buffer} PCM buffer
// Enqueue some PCM data for playback
//
Audio.prototype.play = function play(data) {
  try {
    var pcm = this.opus.decode(data ? data : null);

    if (pcm.length) {
      this.computeGain(pcm);
      this.audio.applyGain(pcm, this.gain);
    }
    this.audio.enqueue(pcm);
  } catch (e) {
    this.emit('error', e);
  }
};

//
// ### function pmix (data)
// #### @id {String} Channel id
// #### @data {Buffer} PCM buffer
// Enqueue some PCM data for playback
//
Audio.prototype.mix = function mix(id, data) {
  // TODO: Mix channels!
  this.play(data);
};
