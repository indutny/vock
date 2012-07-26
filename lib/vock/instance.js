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
  this.jitter = vock.jitter.create(options.jitterWindow || 50);

  // Create audio unit
  this.audio = vock.audio.create(options.rate || 8000);

  this.socket = vock.socket.create();

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
    console.log(packet);
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

    self.socket.send(packet, self.target);
  });

  this.protocol.on('connect', function() {
    self.rtarget = lastRinfo;
    self.audio.start();
  });

  this.protocol.on('close', function() {
    self.audio.stop();
  });

  // Play received data
  this.protocol.on('voice', function(data) {
    self.audio.play(data);
  });

  // Propogate errors from protocol to
  this.protocol.on('error', function(err) {
    self.emit('error', err);
  });
};

Instance.prototype.connect = function connect(port, host, callback) {
  var self = this;

  this.target = {
    port: port,
    host: host
  };
  this.protocol.connect();
  this.protocol.once('connect', function() {
    callback && callback.call(self);
  });
};
