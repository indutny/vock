var vock = require('../vock'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var protocol = exports;

function Protocol() {
  EventEmitter.call(this);

  this.seq = 0;
  this.version = [0,1];
  this.state = 'idle';

  this.pingInterval = 3000;
  this.timeout = 15000;

  this.init();
};
util.inherits(Protocol, EventEmitter);

protocol.create = function() {
  return new Protocol();
};

Protocol.prototype.init = function init() {
  var self = this,
      ping,
      death;

  function resetTimeout() {
    if (death) clearTimeout(death);
    death = setTimeout(function() {
      self.close();
    }, self.timeout);
  };

  this.on('connect', function() {
    ping= setInterval(function() {
      self.write({ type: 'ping' });
    }, self.pingInterval);

    resetTimeout();
  });
  this.on('keepalive', resetTimeout);

  this.on('close', function() {
    if (ping) clearInterval(ping);
    if (death) clearTimeout(death);
  });
};

Protocol.prototype.write = function write(packet) {
  packet.seq = this.seq++;
  this.emit('data', packet);
};

Protocol.prototype.sendVoice = function sendVoice(data) {
  if (this.state !== 'accepted') return;
  this.write({ type: 'voic', data: data });
};

Protocol.prototype.reset = function reset() {
  this.state = 'idle';
  this.write({ type: 'clse', reason: 'reset' });
};

Protocol.prototype.receive = function receive(packet) {
  this.emit('keepalive');

  if (packet.type === 'helo') {
    this.handleHello(packet);
  } else if (packet.type === 'acpt') {
    this.handleAccept(packet);
  } else if (packet.type === 'ping') {
    this.handlePing(packet);
  } else if (packet.type === 'pong') {
    // Ignore pongs
  } else if (packet.type === 'voic') {
    this.handleVoice(packet);
  } else if (packet.type === 'clse') {
    this.handleClose(packet);
  }
};

Protocol.prototype.handleHello = function handleHello(packet) {
  if (this.state !== 'idle' && this.state !== 'accepted') return this.reset();
  this.write({ type: 'acpt', replyTo: packet.seq });
  if (this.state !== 'accepted') this.connect();
};

Protocol.prototype.handleAccept = function handleAccept(packet) {
  if (this.state !== 'idle') return this.reset();
  this.state = 'accepted';
  this.emit('connect');
};

Protocol.prototype.handlePing = function handlePing(packet) {
  if (this.state !== 'accepted') return this.reset();
  this.write({ type: 'pong', replyTo: packet.seq });
};

Protocol.prototype.handleVoice = function handleVoice(packet) {
  if (this.state !== 'accepted') return this.reset();
  this.emit('voice', packet.data);
};

Protocol.prototype.handleClose = function handleClose(packet) {
  if (this.state !== 'accepted') return this.reset();
  this.state = 'idle';
  this.emit('close');
};

Protocol.prototype.close = function close() {
  if (this.state !== 'accepted') return;
  this.state = 'idle';
  this.write({ type: 'clse' });
  this.emit('close');
};

Protocol.prototype.connect = function connect(callback) {
  this.write({ type: 'helo', version: this.version });
  if (callback) this.once('connect', callback);
};
