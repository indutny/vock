var vock = require('../vock'),
    util = require('util'),
    binding = require('../../build/Release/vock'),
    EventEmitter = require('events').EventEmitter,
    Buffer = require('buffer').Buffer;

var audio = exports;

function Audio(rate) {
  EventEmitter.call(this);

  this.audio = new binding.Audio(rate, rate / 50);
  this.opus = new binding.Opus(rate, 1);
  this.active = false;
  this.last = null;
  this.empty = new Buffer(0);
  this.gain = 1;
  this.maxRms = 1500;

  this._removeCallbacks();
};
util.inherits(Audio, EventEmitter);

exports.create = function create(rate) {
  return new Audio(rate);
};

Audio.prototype._removeCallbacks = function removeCallbacks() {
  this.audio.ondata = function() {};
  this.audio.oninputready = function() {};
  this.audio.onoutputready = function() {};
};

Audio.prototype.computeGain = function(pcm) {
  var rms = this.audio.getRms(pcm);
  if (rms > this.maxRms) {
    this.gain = this.maxRms / rms;
  } else {
    // Return gain back to normal
    this.gain = (1 + 20 * this.gain) / 21;
  }
};

Audio.prototype.start = function start() {
  var self = this,
      waiting = 2;

  function finish() {
    if (--waiting !== 0) return;
    self.active = true;
    self.audio.ondata = self.ondata.bind(self);
  };

  this.audio.oninputready = finish;
  this.audio.onoutputready = finish;
  this.audio.start();
};

Audio.prototype.stop = function stop() {
  this.active = false;
  this.last = null;
  this._removeCallbacks();
  this.audio.stop();
};

Audio.prototype.ondata = function ondata(pcm) {
  try {
    if (this.last) pcm = this.audio.cancelEcho(this.last, pcm);
    this.emit('data', this.opus.encode(pcm));
  } catch (e) {
    this.emit('error', e);
  }
};

Audio.prototype.play = function play(data) {
  try {
    var pcm = data ? this.opus.decode(data) : this.empty;
    if (pcm.length) this.last = pcm;

    if (pcm.length !== 0) {
      this.computeGain(pcm);
      this.audio.applyGain(pcm, this.gain);
    }
    this.audio.enqueue(pcm);
  } catch (e) {
    this.emit('error', e);
  }
};
