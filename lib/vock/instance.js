var vock = require('../vock'),
    util = require('util'),
    dgram = require('dgram'),
    msgpack = require('msgpack-js'),
    EventEmitter = require('events').EventEmitter;

var instance = exports;

function Instance(options) {
  EventEmitter.call(this);
  if (!options) options = {};

  this.target = null;
  this.rtarget = null;
  this.protocol = vock.protocol.create();
  this.jitter = vock.jitter.create(options.jitterWindow || 100);

  // Create audio unit
  this.audio = vock.audio.create(options.rate || 24000);

  this.socket = vock.socket.create(options);
  this.api = vock.api.create(this, options.server);
  this.mode = 'direct';

  this.init(options);
};
util.inherits(Instance, EventEmitter);

instance.create = function create(options) {
  return new Instance(options);
};

Instance.prototype.init = function init(options) {
  var self = this,
      lastRinfo;

  this.socket.on('error', function(err) {
    self.emit('error', err);
  });

  // Pass packets from other side to protocol parser
  this.socket.on('data', function(packet, rinfo) {
    if (!self.target) return;
    if (self.rtarget &&
        (self.rtarget.address !== rinfo.address ||
         self.rtarget.port !== rinfo.port)) {
      // Connection came from different address
      return;
    }
    lastRinfo = rinfo;
    self.jitter.write(packet);
  });

  this.jitter.on('data', function(packet) {
    try {
      self.protocol.receive(packet);
    } catch (e) {
      self.emit('error', e);
    }
  });

  if (!options.mute) {
    this.audio.on('data', function(data) {
      self.protocol.sendVoice(data);
    });
  }

  // Pass packets from protocol to other side
  this.protocol.on('data', function(packet) {
    if (!self.target) return;

    if (self.mode === 'direct') {
      self.socket.send(packet, self.target);
    } else if (self.mode === 'relay') {
      self.socket.relay(packet, self.target);
    }
  });

  this.protocol.on('connect', function() {
    self.rtarget = lastRinfo;
    self.audio.start();
  });

  this.protocol.on('close', function() {
    try {
      self.audio.stop();
    } catch(e) {
      // Ignore
    }
  });

  // Play received data
  this.protocol.on('voice', function(data) {
    self.audio.play(data);
  });

  this.audio.on('error', function(err) {
    self.emit('error', err);
  });

  // Propogate errors from protocol to
  this.protocol.on('error', function(err) {
    self.emit('error', err);
  });
};

Instance.prototype.connect = function connect(id, port, address, callback) {
  var self = this;

  this.target = {
    id: id,
    port: port,
    address: address
  };
  this.protocol.connect(function() {
    self.protocol.removeListener('connectTimeout', onConnectTimeout);
    callback && callback.call(self);
  });
  this.protocol.once('connectTimeout', onConnectTimeout);

  function onConnectTimeout() {
    // Switch to relay mode
    self.mode = 'relay';
    self.connect(id, port, address, callback);
  }
};
