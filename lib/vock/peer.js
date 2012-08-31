var vock = require('../vock'),
    crypto = require('crypto'),
    pripub = require('pripub'),
    fs = require('fs'),
    msgpack = require('msgpack-js'),
    util = require('util'),
    EventEmitter = require('events').EventEmitter;

var peer = exports;

var groups = {
  'handshake': 0,
  'voice': 1,
  'text': 2,
  'ackn': 3
};

var id = 0;

/*
 * States:
 *  - init
 *  - idle
 *  - accepted
 *  - close
 *
 * Possible state transitions:
 *
 *  init -> idle
 *
 *  idle -> accepted, close
 *
 *  accepted -> close
 */

//
// ### function Peer (options)
// #### @options {Object} Peer options
// Vock peer constructor
//
function Peer(options) {
  EventEmitter.call(this);

  var self = this;

  this.id = id++;
  this.options = options;

  // Protocol configurations
  this.version = [0,1];
  this.seqs = {};
  this.recSeqs = {};
  this.lastVoice = 0;

  // Timeouts
  this.intervals = {
    ping: 500,
    handshake: 333,
    connect: 3000,
    death: 15000,
    wait: 600,
    rel: 1000,
    relCleanup: 60 * 1000
  };

  // Timers
  this.timers = {
    ping: null,
    handshake: null,
    connect: null,
    death: null,
    wait: null
  };

  // Communication options
  this.mode = 'direct';
  this.jitter = vock.jitter.create(33);
  this.connecting = false;

  // Encryption options
  this.pripub = pripub.create({
    pri: options.keyFile && fs.readFileSync(options.keyFile),
    password: options.password
  });

  this.dh = crypto.getDiffieHellman('modp14');
  this.dh.generateKeys();
  this.secret = null;

  this.init();
  this.resetSeqs();

  // Load private key
  this.state = 'init';
  this.pripub.init(function(err) {
    if (err) return self.emit('error', err);

    self.state = 'idle';
    self._initQueue.forEach(function(callback) {
      callback.call(self);
    });
  });
  this._initQueue = [];
};
util.inherits(Peer, EventEmitter);

//
// ### function create (options)
// #### @options {Object} Peer options
// Constructor wrapper
//
peer.create = function(options) {
  return new Peer(options);
};

// Internal
Peer.prototype.setTimeout = function _setTimeout(fn, type) {
  if (!this.intervals[type]) throw new Error('Unknown interval: ' + type);
  this.clearTimeout(type);
  this.timers[type] = setTimeout(fn, this.intervals[type]);
};

// Internal
Peer.prototype.setInterval = function _setInterval(fn, type) {
  if (!this.intervals[type]) throw new Error('Unknown interval: ' + type);
  this.clearInterval(type);
  this.timers[type] = setInterval(fn, this.intervals[type]);
  fn();
};

// Internal
Peer.prototype.clearTimeout = function _clearTimeout(type) {
  if (this.timers[type]) clearTimeout(this.timers[type]);
};

// Internal
Peer.prototype.clearInterval = function _clearInterval(type) {
  if (this.timers[type]) clearInterval(this.timers[type]);
};

//
// ### function resetSeqs ()
// Reset sequence numbers for all packet types
// (NOTE: Also resets state)
//
Peer.prototype.resetSeqs = function resetSeqs() {
  // Sequence numbers (sent/received)
  Object.keys(groups).forEach(function(key) {
    this.seqs[groups[key]] = 0;
    this.recSeqs[groups[key]] = 0;
  }, this);

  // Reliable transport data
  this.rseen = {};
  this.rseq = 0;

  this.lastVoice = 0;
  this.state = 'idle';
};

//
// ### function init ()
// Initialize peer instance.
// Set ping and death timers
//
Peer.prototype.init = function init() {
  var self = this;

  function resetTimeout() {
    self.setTimeout(function() {
      self.reset();
    }, 'death');
  };

  this.on('connect', function() {
    self.setInterval(function() {
      self.write('handshake', { type: 'ping' });
    }, 'ping');

    resetTimeout();
  });
  this.on('keepalive', resetTimeout);

  this.on('close', function(reason) {
    self.clearInterval('ping');
    self.clearTimeout('death');

    self.clearInterval('wait');
    self.clearTimeout('connect');
    self.clearInterval('handshake');
  });

  // Use packets that jitter has emitted out
  this.jitter.on('data', function(packet) {
    try {
      self.onJitterData(packet);
    } catch (e) {
      self.emit('error', e);
    }
  });
};

//
// ### function write (group, packet)
// #### @group {String} Group ID
// #### @packet {Object} Packet
// Internal method for sending packets to the opponent
//
Peer.prototype.write = function write(group, packet) {
  packet.group = groups[group];
  packet.seq = this.seqs[packet.group]++;

  if (!this.secret || group === 'handshake') {
    this.emit('data', packet);
  } else {
    var cipher = crypto.createCipher('aes256', 'hakamada'),
        packed = msgpack.encode(packet),
        enc = cipher.update(packed.toString('base64'), 'utf-8', 'hex') +
              cipher.final('hex');

    this.emit('data', new Buffer(enc, 'hex'));
  }

  return packet.seq;
};

//
// ### function rwrite (group, packet, callback)
// #### @group {String} Group ID
// #### @packet {Object} Packet
// #### @callback {Function} Continuation to proceed too
// Send packet reliably
//
Peer.prototype.rwrite = function rwrite(group, packet, callback) {
  var self = this,
      rseq = this.rseq++,
      interval;

  packet.rseq = rseq;

  // Once 'ack' is received - invoke callback
  function onack() {
    clearInterval(interval);
    callback && callback(null, rseq);
  }
  this.once('ack:' + rseq, onack);

  // Try sending packets until 'ack' will be received
  function send() {
    self.write(group, packet);
  }
  interval = setInterval(send, this.intervals.rel);
  send();

  // Give up after timeout
  setTimeout(function() {
    clearInterval(interval);

    self.removeListener('ack:' + rseq, onack);
  }, this.intervals.relCleanup);

  return rseq;
};

//
// ### function sendVoice (data)
// #### @data {Buffer} PCM data
// Send voice data
//
Peer.prototype.sendVoice = function sendVoice(data) {
  if (this.state !== 'accepted') return;
  this.write('voice', {
    type: 'voic',
    data: data
  });
};

//
// ### function sendText (text)
// #### @text {String} message text
// Send text data
//
Peer.prototype.sendText = function sendText(text) {
  if (this.state !== 'accepted') return;
  this.rwrite('text', {
    type: 'text',
    text: text
  });
};

//
// ### function reset ()
// Resets state and sends close packet
//
Peer.prototype.reset = function reset() {
  if (this.state === 'closed') return;

  this.state = 'closed';
  this.write('handshake', { type: 'clse', reason: 'reset' });
  this.emit('close', 'reset');
};

//
// ### function receive (packet)
// #### @packet {Object} Incoming packet
// Handle incoming packet
//
Peer.prototype.receive = function receive(packet) {
  // API and Relay protocols should not get there
  if (packet.protocol) return;

  if (packet instanceof Buffer) {
    // Ignore encrypted packets before handshake is finished
    if (!this.secret) return;

    // Decrypt packet
    var decipher = crypto.createDecipher('aes256', 'hakamada'),
        dec = decipher.update(packet.toString('hex'), 'hex', 'utf-8') +
              decipher.final('utf-8');

    packet = msgpack.decode(new Buffer(dec, 'base64'));
  }

  // Put packet into jitter buffer
  this.jitter.write(packet);
};

//
// ### function onJitterData (packet)
// #### @packet {Object} Protocol packet
// Process packets emitted by jitter
//
Peer.prototype.onJitterData = function onJitterData(packet) {
  var self = this;

  // Once closed - ignore everything incoming
  if (this.state === 'closed') return;

  this.emit('keepalive');

  // Try connecting to peer
  if (this.state !== 'accepted') this.connect();

  // Ignore packets while initializing
  if (this.state === 'init') return;

  // If group is unknown - ignore packet
  if (this.recSeqs[packet.group] === undefined) return;

  // Voice packets should come in a strict order
  if (packet.group === groups.voice &&
      this.recSeqs[packet.group] + 1 !== packet.seq) {
    // Oh, we lost some voice packets - notify backend about it
    this.emit('voice', null);
  }

  // Seq should be monotonic
  if (packet.rseq === undefined && this.recSeqs[packet.group] > packet.seq) {
    return;
  }

  // Set received sequence number
  this.recSeqs[packet.group] = packet.seq;

  // If packet has rseq number and isn't ACKnowledge
  if (packet.rseq !== undefined && packet.group !== groups.ackn) {
    // Send ACK to remote side
    this.write('ackn', { type: 'ackn', rseq: packet.rseq });

    // But ignore packets that were already delivered
    if (this.rseen[packet.rseq]) return;
    this.rseen[packet.rseq] = true;

    // Cleanup hashmap after some time
    setTimeout(function() {
      delete self.rseen[packet.rseq];
    }, this.intervals.relCleanup);
  }

  if (packet.type === 'helo') {
    this.handleHello(packet);
  } else if (packet.type === 'wait') {
    this.handleWait(packet);
  } else if (packet.type === 'acpt') {
    this.handleAccept(packet);
  } else if (packet.type === 'ping') {
    this.handlePing(packet);
  } else if (packet.type === 'pong') {
    // Ignore pongs
  } else if (packet.type === 'voic') {
    this.handleVoice(packet);
  } else if (packet.type === 'text') {
    this.handleText(packet);
  } else if (packet.type === 'ackn') {
    this.handleAck(packet);
  } else if (packet.type === 'clse') {
    this.handleClose(packet);
  }
};

//
// ### function handleHello (packet)
// #### @packet {Object} Packet
// Handle HELO packet
//
Peer.prototype.handleHello = function handleHello(packet) {
  var self = this;

  if (this.state !== 'idle' && this.state !== 'accepted') {
    return this.reset();
  }

  // Disallow non-encrypted packets (i.e. without public key)
  if (!packet.public) return this.reset();

  if (!this.roomId) {
    this.roomId = packet.roomId;
  } else if (this.roomId != packet.roomId) {
    // Room id mismatch!
    return this.reset();
  }

  var fingerprint = crypto.createHash('sha1').update(packet.public)
                                             .digest('hex');

  // Notify remote that we've received everything and are just waiting
  this.setInterval(function() {
    self.write('handshake', { type: 'wait' });
  }, 'wait');

  // Ask instance if this fingerprint is known and authorized
  this.emit('authorize', fingerprint, function(ok) {
    self.clearInterval('wait');

    if (!ok) return;

    // Handshake
    pripub.create({
      pri: self.options.keyFile && fs.readFileSync(self.options.keyFile),
      pub: packet.public,
      password: self.options.password
    }).init(function(err, p) {
      if (err) return self.emit('error', err);

      self.fingerprint = fingerprint;
      self.write('handshake', {
        type: 'acpt',
        dh: new Buffer(p.encrypt(self.dh.getPublicKey()), 'binary')
      });

      if (!self.connecting) self.connect(self.roomId);
    });
  });
};

//
// ### function handleWait (packet)
// #### @packet {Object} Packet
// Handle WAIT packet
//
Peer.prototype.handleWait = function handleHello(packet) {
  this.clearTimeout('connect');
};

//
// ### function handleAccept (packet)
// #### @packet {Object} Packet
// Handle ACPT packet
//
Peer.prototype.handleAccept = function handleAccept(packet) {
  if (this.state === 'accepted' || this.state === 'init') return;

  // Disallow non-encrypted packets
  // (remote side should send diffie-hellman public key)
  if (!packet.dh) return this.reset();

  // Compute diffie-hellman secret
  this.secret = new Buffer(
    this.dh.computeSecret(
      this.pripub.decrypt(packet.dh).toString()
    ),
    'binary'
  );

  this.state = 'accepted';
  this.emit('connect');
};

//
// ### function handlePing (packet)
// #### @packet {Object} Packet
// Handle PING packet
//
Peer.prototype.handlePing = function handlePing(packet) {
  this.write('handshake', { type: 'pong' });
};

//
// ### function handleVoice (packet)
// #### @packet {Object} packet
// Handle VOIC packet
//
Peer.prototype.handleVoice = function handleVoice(packet) {
  if (this.state !== 'accepted') return;

  this.emit('voice', packet.data);
};

//
// ### function handleText (packet)
// #### @packet {Object} packet
// Handle TEXT packet
//
Peer.prototype.handleText = function handleText(packet) {
  if (this.state !== 'accepted') return;

  this.emit('text', packet.text);
};

//
// ### function handleAck (packet)
// #### @packet {Object} packet
// Handle ACKN packet
//
Peer.prototype.handleAck = function handleAck(packet) {
  this.emit('ack:' + packet.rseq);
};

//
// ### function handleClose (packet)
// #### @packet {Object} packet
// Handle CLSE packet
//
Peer.prototype.handleClose = function handleClose(packet) {
  if (this.state !== 'accepted') {
    if (packet.reason !== 'reset') this.reset();
    return;
  }

  if (this.state !== 'idle' && this.state !== 'closed') {
    this.state = 'closed';
    this.write('handshake', { type: 'clse' });
    this.emit('close', 'external');
  }
};

//
// ### function close ()
// Send close packet
//
Peer.prototype.close = function close(reason) {
  this.state = 'closed';
  this.write('handshake', { type: 'clse', reason: reason || 'local' });
  this.emit('close', reason || 'local');
};

//
// ### function connect(id, callback)
// #### @id {String } Room id
// #### @callback {Function} continuation to proceed too
// Connect to opponent
//
Peer.prototype.connect = function connect(id, callback) {
  // If we're initializing - queue request
  if (this.state === 'init') {
    this._initQueue.push(function() {
      this.connect(id, callback);
    });
    return;
  }

  // If we're already connecting - ignore request
  if (this.connecting) return;
  this.connecting = true;

  var self = this,
      packet = {
        type: 'helo',
        roomId: id,
        version: self.version,
        public: this.pripub.getPublicKey()
      };

  this.roomId = id;

  // Try handshaking multiple times
  function send() {
    self.write('handshake', packet);
  }
  this.setInterval(send, 'handshake');

  this.setTimeout(function() {
    // Fail after timeout
    self.removeListener('connect', onConnect);
    self.connecting = false;

    self.clearInterval('handshake');
    self.clearTimeout('connect');

    // Do not try reconnecting if we've already tried switching modes
    if (self.mode !== 'relay') {
      // Change to relay mode
      self.mode = 'relay';
      self.intervals.connect = 2 * self.intervals.connect;

      // And try reconnecting
      self.connect(id, callback);
    } else {
      self.close('timeout');
    }
  }, 'connect');

  this.once('connect', onConnect);

  function onConnect() {
    callback && callback();

    self.removeListener('connect', onConnect);
    self.clearInterval('handshake');
    self.clearTimeout('connect');
  }
};
