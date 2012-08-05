var vock = require('../vock'),
    util = require('util'),
    dgram = require('dgram'),
    EventEmitter = require('events').EventEmitter;

var instance = exports;

//
// ### function Instance (options)
// #### @options {Object} instance options
// Vock connection handler instance.
//
function Instance(options) {
  EventEmitter.call(this);

  this.options = options;

  // Create audio unit
  this.audio = vock.audio.create(this.options.rate || 48000);
  this.audio.start();

  this.socket = vock.socket.create(this.options);
  this.api = vock.api.create(this, this.options.server);

  this.authActive = false;
  this.authQueue = [];
  this.authCache = {};

  // Peers hashmap
  this.peers = {};
  this.peerIndexes = [];
  for (var i = 0; i < 64; i++) {
    this.peerIndexes.push(i);
  }

  this.init();
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
// ### function init ()
// Initialize instance
//
Instance.prototype.init = function init() {
  var self = this,
      lastRinfo;

  // Propagate socket errors to instance
  this.socket.on('error', function(err) {
    self.emit('error', err);
  });

  // Put packets in jitter buffer first
  // (to reorder them before using)
  this.socket.on('data', function(packet, rinfo) {
    // Ignore server packets
    if ((rinfo.address === self.options.server.address &&
        rinfo.port === self.options.server.port) ||
        packet.protocol) {
      return;
    }

    // Deliver packet to peer
    self.getPeer(rinfo).receive(packet);
  });

  // Propagate audio errors to instance level
  this.audio.on('error', function(err) {
    self.emit('error', err);
  });
};

//
// ### function getPeer (info)
// #### @info {Object} peer info (address, port)
// Return peer object
//
Instance.prototype.getPeer = function getPeer(info) {
  var id = this.getId(info);

  // Reuse existing peer if present
  if (this.peers.hasOwnProperty(id)) {
    return this.peers[id];
  }

  // Otherwise create new
  var self = this,
      peer = vock.peer.create(this.options);

  this.emit('peer:create', info);

  // Get peer's index
  var index = this.peerIndexes.pop();
  if (index === undefined) {
    peer.reset();
    return;
  }

  this.peers[id] = peer;

  // Attach audio to peer
  if (!this.options.mute) {
    function onAudio(data) {
      peer.sendVoice(data);
    }
    this.audio.on('data', onAudio);
  }

  // Mix in peer's voice data
  peer.on('voice', function(frame) {
    self.audio.play(index, frame);
  });

  // Send data to socket
  peer.on('data', function(packet) {
    if (peer.mode === 'direct') {
      // Pass packets directly
      self.socket.send(packet, info);
    } else if (peer.mode === 'relay') {
      // Pass packets through server
      self.socket.relay(packet, peer.roomId, info);
    }
  });

  // Propagate authorization requests
  peer.on('authorize', function(fingerprint, callback) {
    self.isAuthorized(fingerprint, callback);
  });

  // Propagate timeout
  peer.on('close', function(reason) {
    self.emit('peer:close', info, reason);
  });

  // Detach peer on close
  peer.once('close', function() {
    delete self.peers[id];
    self.peerIndexes.push(index);
    self.audio.removeListener('data', onAudio);
  });

  // Handle peer errors
  peer.on('error', function(e) {
    self.emit(e);
    peer.emit('close');
  });

  peer.once('connect', function() {
    self.emit('peer:connect', info, {
      mode: peer.mode
    });
  });

  return peer;
};

//
// ### function isAuthorized (fingerprint, callback)
// #### @fingerprint {String} Hex fingerprint
// #### @callback {Function} function (auhorized /* true|false */) {}
// Asks instance if this fingerprint is authorized
//
Instance.prototype.isAuthorized = function isAuthorized(fingerprint, callback) {
  // Handle cache hits
  if (this.authCache.hasOwnProperty(fingerprint)) {
    return callback(this.authCache[fingerprint]);
  }

  this.authQueue.push({
    fingerprint: fingerprint,
    callback: callback
  });

  if (this.authActive) return;
  this.authActive = true;

  var self = this;
  this.emit('authorize', fingerprint, function(ok) {
    self.authCache[fingerprint] = ok;

    self.authQueue = self.authQueue.filter(function (item) {
      if (item.fingerprint === fingerprint) {
        item.callback(ok);
        return false;
      }
      return true;
    });

    var next = self.authQueue.shift();

    if (!next) {
      self.authActive = false;
      return;
    }

    self.isAuthorized(next.fingerprint, next.callback);
  });
};

//
// ### function getId (info)
// #### @info {Object} peer info
// Return stringified id of peer
//
Instance.prototype.getId = function getId(info) {
  return info.address + '#' + info.port;
};

//
// ### function isKnown (info)
// #### @info {Object} peer info
// Return true if we're trying to connect to this peer
//
Instance.prototype.isKnown = function isKnown(info) {
  return this.peers.hasOwnProperty(this.getId(info));
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
  var self = this,
      peer = this.getPeer({
        port: port,
        address: address
      });

  // Initiate connect sequence
  peer.connect(id, function() {
    callback && callback.call(self);
  });
};
