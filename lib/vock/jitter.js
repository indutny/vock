var vock = require('../vock'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var jitter = exports;

function JitterBuffer(delay) {
  EventEmitter.call(this);
  this.packets = [];
  this.delay = delay;
};
util.inherits(JitterBuffer, EventEmitter);

jitter.create = function create() {
  return new JitterBuffer();
};

JitterBuffer.prototype.write = function write(packet) {
  var self = this;

  this.packets.push(packet);
  this.packets.sort(function(a, b) {
    return a.group === b.group ? b.seq - a.seq : a.group > b.group ? 1 : -1;
  });

  setTimeout(function() {
    self.emit('data', self.packets.pop());
  }, this.delay);
};
