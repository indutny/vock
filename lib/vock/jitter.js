var vock = require('../vock'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var jitter = exports;

//
// ### function JitterBuffer (delay)
// #### @delay {Number} delay in msec
// Jitter buffer constructor
//
function JitterBuffer(delay) {
  EventEmitter.call(this);
  this.packets = [];
  this.delay = delay;
};
util.inherits(JitterBuffer, EventEmitter);

//
// ### function create (delay)
// #### @delay {Number} delay in msec
// Constructor wrapper
//
jitter.create = function create(delay) {
  return new JitterBuffer(delay);
};

//
// ### function write (packet)
// #### @packet {Object} Vock packet with seq and group
// Puts packet to the jitter buffer
//
JitterBuffer.prototype.write = function write(packet) {
  var self = this;

  // Sort packets in groups by seq
  this.packets.push(packet);
  this.packets.sort(function(a, b) {
    return a.group === b.group ? b.seq - a.seq : a.group > b.group ? 1 : -1;
  });

  // Emit first packet after timeout
  setTimeout(function() {
    self.emit('data', self.packets.pop());
  }, this.delay);
};
