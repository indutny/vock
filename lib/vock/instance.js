var vock = require('../vock'),
    crypto = require('crypto'),
    util = require('util'),
    utile = require('utile'),
    dgram = require('dgram'),
    dht = require('dht.js'),
    Buffer = require('buffer').Buffer,
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
  this.muted = options.mute || false;

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

  // DHT
  this.dht = null;
  this.adIds = {};

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

  // Propagate info
  this.socket.on('nat:traversal:tcp', function(how, port) {
    self.emit('nat:traversal:tcp', how, port);
  });
  this.socket.on('nat:traversal:udp', function(how, port) {
    self.emit('nat:traversal:udp', how, port);
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

    var peer = self.getPeer(rinfo);

    // Switch peer to relay mode if outer node is using it
    if (rinfo.relay && peer.mode !== 'relay') {
      peer.mode = 'relay';
    }

    // Deliver packet to peer
    peer.receive(packet);
  });

  // Initiate dht
  this.socket.once('init', function() {
    self.dht = dht.node.create(utile.mixin({}, self.options.dht || {}, {
      socket: self.socket.socket
    }));

    self.dht.on('peer:new', function(infohash, peer) {
      infohash = infohash.toString('hex');
      var id = self.adIds[infohash];
      if (!id) return;

      self.connect(id, peer.port, peer.address);
    });

    self.dht.on('error', function() {
      // Ignore DHT errors
    });

    self.emit('dht:init');
  });

  // Save dht data periodically
  this.dhtInterval = setInterval(function() {
    if (!self.dht) return;
    self.emit('dht:save', self.dht.save());
  }, 5000);

  // Propagate audio errors to instance level
  this.audio.on('error', function(err) {
    self.emit('error', err);
  });
};

//
// ### function advertise (id)
// #### @id {String} Room id
// Create DHT advertisement
//
Instance.prototype.advertise = function advertise(id) {
  var self = this;

  if (!this.dht) {
    this.once('dht:init', function() {
      self.advertise(id);
    });
    return;
  }

  var hash = crypto.createHash('sha1').update(id).digest('hex');
  this.dht.advertise(new Buffer(hash, 'hex'), this.socket.port);

  this.adIds[hash] = id;
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

  this.emit('peer:create', info, peer.id);

  // Get peer's index
  var index = this.peerIndexes.pop();
  if (index === undefined) {
    peer.reset();
    return;
  }

  this.peers[id] = peer;

  // Attach audio to peer
  function onAudio(data) {
    if (!self.muted) {
      peer.sendVoice(data);
    }
  }
  this.audio.on('data', onAudio);

  // Mix in peer's voice data
  peer.on('voice', function(frame) {
    self.audio.play(index, frame);
  });

  // Broadcast instance's text
  function onText(text) {
    peer.sendText(text);
  }
  this.on('text', onText);

  // Show peer's incoming text
  peer.on('text', function(text) {
    self.emit('peer:text', peer.fingerprint, text);
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

  // Detach peer on close
  peer.once('close', function(reason) {
    delete self.peers[id];
    self.peerIndexes.push(index);
    self.audio.removeListener('data', onAudio);
    self.removeListener('text', onText);

    self.emit('peer:close', info, reason, peer.id);
  });

  // Handle peer errors
  peer.on('error', function(e) {
    peer.close('error');
    self.emit('error', e);
  });

  peer.once('connect', function() {
    self.emit('peer:connect', info, {
      id: peer.id,
      mode: peer.mode
    });
  });

  return peer;
};

//
// ### function sendText (text)
// #### @text {String} Message
// Sends text to all connected peers
//
Instance.prototype.sendText = function sendText(text) {
  this.emit('text', text);
};

//
// ### function toggleMute ()
// Toggles mute state
//
Instance.prototype.toggleMute = function toggleMute() {
  this.muted = !this.muted;
  return this.muted;
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

  function connectDht() {
    self.dht.connect({ port: port, address: address });
  }

  // Inform DHT about new node
  if (this.dht) return connectDht();
  this.once('dht:init', connectDht);
};
