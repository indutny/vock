var vock = require('../vock'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var protocol = exports;

var groups = {
  'handshake': 0,
  'voice': 1
};

function Protocol() {
  EventEmitter.call(this);

  this.version = [0,1];

  this.seqs = {};
  this.recSeqs = {};
  this.lastVoice = 0;
  this.pingInterval = 3000;
  this.handshakeInterval = 1000;
  this.timeout = 15000;
  this.connectTimeout = 5000;

  this.init();
  this.resetSeqs();
};
util.inherits(Protocol, EventEmitter);

protocol.create = function() {
  return new Protocol();
};

Protocol.prototype.resetSeqs = function reset() {
  Object.keys(groups).forEach(function(key) {
    this.seqs[groups[key]] = 0;
    this.recSeqs[groups[key]] = 0;
  }, this);
  this.lastVoice = 0;
  this.state = 'idle';
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
    ping = setInterval(function() {
      self.write('handshake', { type: 'ping' });
    }, self.pingInterval);

    resetTimeout();
  });
  this.on('keepalive', resetTimeout);

  this.on('close', function() {
    if (ping) clearInterval(ping);
    if (death) clearTimeout(death);
  });
};

Protocol.prototype.write = function write(group, packet) {
  packet.group = groups[group];
  packet.seq = this.seqs[packet.group]++;
  this.emit('data', packet);

  return packet.seq;
};

Protocol.prototype.sendVoice = function sendVoice(data) {
  if (this.state !== 'accepted') return;
  this.write('voice', {
    type: 'voic',
    data: data
  });
};

Protocol.prototype.reset = function reset() {
  this.state = 'idle';
  this.write('handshake', { type: 'clse', reason: 'reset' });
};

Protocol.prototype.receive = function receive(packet) {
  // API and Relay protocols should not get there
  if (packet.protocol) return;

  // Seq should be monotonic
  if (this.recSeqs[packet.group] === undefined) return;

  if (packet.group === groups.voice &&
      this.recSeqs[packet.group] + 1 !== packet.seq) {
    // Oh, we lost some voice packets - notify backend about it
    this.emit('voice', null);
  }

  if (this.recSeqs[packet.group] > packet.seq) {
    return;
  }
  this.recSeqs[packet.group] = packet.seq;

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
  this.write('handshake', { type: 'acpt' });
  if (this.state !== 'accepted') this.connect();
};

Protocol.prototype.handleAccept = function handleAccept(packet) {
  if (this.state !== 'idle') return this.reset();
  this.state = 'accepted';
  this.emit('connect');
};

Protocol.prototype.handlePing = function handlePing(packet) {
  if (this.state !== 'accepted') return this.reset();
  this.write('handshake', { type: 'pong' });
};

Protocol.prototype.handleVoice = function handleVoice(packet) {
  if (this.state !== 'accepted') return this.reset();

  this.emit('voice', packet.data);
};

Protocol.prototype.handleClose = function handleClose(packet) {
  if (this.state !== 'accepted') return this.reset();
  this.emit('close');

  // Reset sequence numbers
  this.resetSeqs();
};

Protocol.prototype.close = function close() {
  if (this.state !== 'accepted') return;
  this.state = 'idle';
  this.write('handshake', { type: 'clse' });
  this.emit('close');
};

Protocol.prototype.connect = function connect(callback) {
  var self = this;

  function send() {
    self.write('handshake', { type: 'helo', version: this.version });
  }
  var interval = setInterval(send, this.handshakeInterval);
  send();

  var timeout = setTimeout(function() {
    self.emit('connectTimeout');
    self.removeListener('connect', onConnect);
    clearInterval(interval);
    clearTimeout(timeout);
  }, this.connectTimeout);

  this.once('connect', onConnect);

  function onConnect() {
    callback && callback();

    self.removeListener('connect', onConnect);
    clearInterval(interval);
    clearTimeout(timeout);
  }
};
