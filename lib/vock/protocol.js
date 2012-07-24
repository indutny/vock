var vock = require('../vock'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var protocol = exports;

function Protocol() {
  EventEmitter.call(this);

  this.seq = 0;
  this.version = [0,1];
  this.state = 'idle';

  this.retryInterval = 5000;
};
util.inherits(Protocol, EventEmitter);

protocol.create = function() {
  return new Protocol();
};

Protocol.prototype.write = function write(packet) {
  packet.seq = this.seq++;
  packet.version = this.version;
  this.emit('data', packet);
};

Protocol.prototype.sendVoice = function sendVoice(data) {
  if (this.state !== 'accepted') return;
  this.write({ type: 'voic', data: data });
};

Protocol.prototype.receive = function receive(packet) {
  if (packet.type === 'helo') {
    this.handleHello(packet);
  } else if (packet.type === 'acpt') {
    this.handleAccept(packet);
  } else if (packet.type === 'ping') {
    this.handlePing(packet);
  } else if (packet.type === 'voic') {
    this.handleVoice(packet);
  } else if (packet.type === 'clse') {
    this.handleClose(packet);
  }
};

Protocol.prototype.handleHello = function handleHello(packet) {
  this.write({ type: 'acpt', replyTo: packet.seq });
};

Protocol.prototype.handleAccept = function handleAccept(packet) {
  if (this.state !== 'waiting') return;
  this.state = 'accepted';
  this.emit('connect');
};

Protocol.prototype.handlePing = function handlePing(packet) {
  // Ignore ping for now
};

Protocol.prototype.handleVoice = function handleVoice(packet) {
  if (this.state !== 'accepted') return;
  this.emit('voice', packet.data);
};

Protocol.prototype.handleClose = function handleClose(packet) {
  if (this.state !== 'accepted') return;
  this.state = 'idle';
  this.emit('close');
};

Protocol.prototype.connect = function connect(callback) {
  if (this.state !== 'idle') return;
  this.state = 'waiting';

  var self = this,
      i = setInterval(tick, this.retryInterval);

  tick();
  function tick() {
    if (self.state !== 'waiting') {
      clearInterval(i);
      return;
    }
    self.write({ type: 'helo' });
  }
};
