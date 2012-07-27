var vock = require('../vock'),
    util = require('util'),
    dgram = require('dgram'),
    msgpack = require('msgpack-js'),
    EventEmitter = require('events').EventEmitter;

var instance = exports;

//
// ### function Instance (options)
// #### @options {Object} instance options
// Vock connection handler instance.
//
function Instance(options) {
  EventEmitter.call(this);
  if (!options) options = {};

  this.target = null;
  this.rtarget = null;
  this.protocol = vock.protocol.create();
  this.jitter = vock.jitter.create(options.jitterWindow || 50);

  // Create audio unit
  this.audio = vock.audio.create(options.rate || 24000);

  this.socket = vock.socket.create(options);
  this.api = vock.api.create(this, options.server);
  this.mode = 'direct';

  this.init(options);
};
util.inherits(Instance, EventEmitter);

//
// ### function create (options)
// #### @options {Object} instance options
// Wrapper for creating instance
//
instance.create = function create(options) {
  return new Instance(options);
};

//
// ### function init (options)
// #### @options {Object} instance options
// Initialize instance
//
Instance.prototype.init = function init(options) {
  var self = this,
      lastRinfo;

  // Propagate socket errors to instance
  this.socket.on('error', function(err) {
    self.emit('error', err);
  });

  // Put packets in jitter buffer first
  // (to reorder them before using)
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

  // Use packets that jitter has emitted out
  this.jitter.on('data', function(packet) {
    try {
      self.protocol.receive(packet);
    } catch (e) {
      self.emit('error', e);
    }
  });

  // Send Opus-encoded data from microphone to recipient
  if (!options.mute) {
    this.audio.on('data', function(data) {
      self.protocol.sendVoice(data);
    });
  }

  // Pass packets from protocol to recipient
  this.protocol.on('data', function(packet) {
    if (!self.target) return;

    if (self.mode === 'direct') {
      // Pass packets directly
      self.socket.send(packet, self.target);
    } else if (self.mode === 'relay') {
      // Pass packets through server
      self.socket.relay(packet, self.target);
    }
  });

  // When connected to other side
  this.protocol.on('connect', function() {
    try {
      // Set target
      self.rtarget = lastRinfo;
      // And start recording/playing audio
      self.audio.start();
    } catch (e) {
      // Ignore
    }
  });

  // When disconnected
  this.protocol.on('close', function() {
    try {
      // Stop recording
      self.audio.stop();
    } catch(e) {
      // Ignore
    }
  });

  // Play received data
  this.protocol.on('voice', function(data) {
    self.audio.play(data);
  });

  // Propagate audio errors to instance level
  this.audio.on('error', function(err) {
    self.emit('error', err);
  });

  // Propagate errors from protocol to
  this.protocol.on('error', function(err) {
    self.emit('error', err);
  });
};

//
// ### function connect (id, port, address, callback)
// #### @id {String} Room id
// #### @port {Number} Recipient's port
// #### @address {String} Recipient's IP
// #### @callback {Function} Continuation to proceed too
// Connect to other peer
//
Instance.prototype.connect = function connect(id, port, address, callback) {
  var self = this;

  // Store target to limit incoming packet sources
  this.target = {
    id: id,
    port: port,
    address: address
  };

  // Initiate connect sequence
  this.protocol.connect(function() {
    self.protocol.removeListener('connectTimeout', onConnectTimeout);
    callback && callback.call(self);
  });

  // On connection timeout switch to relay mode
  this.protocol.once('connectTimeout', onConnectTimeout);
  function onConnectTimeout() {
    // Switch to relay mode
    self.mode = 'relay';
    self.connect(id, port, address, callback);
  }
};
