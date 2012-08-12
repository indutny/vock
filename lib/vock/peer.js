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
  'text': 2
};

//
// ### function Peer (options)
// #### @options {Object} Peer options
// Vock peer constructor
//
function Peer(options) {
  EventEmitter.call(this);

  var self = this;

  this.options = options;

  // Protocol configurations
  this.version = [0,1];
  this.seqs = {};
  this.recSeqs = {};
  this.lastVoice = 0;
  this.pingInterval = 3000;
  this.handshakeInterval = 1000;
  this.timeout = 15000;
  this.connectTimeout = 3000;
  this._connectTimeout = null;
  this.waitInterval = 2000;

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

//
// ### function resetSeqs ()
// Reset sequence numbers for all packet types
// (NOTE: Also resets state)
//
Peer.prototype.resetSeqs = function resetSeqs() {
  Object.keys(groups).forEach(function(key) {
    this.seqs[groups[key]] = 0;
    this.recSeqs[groups[key]] = 0;
  }, this);
  this.lastVoice = 0;
  this.state = 'idle';
};

//
// ### function init ()
// Initialize peer instance.
// Set ping and death timers
//
Peer.prototype.init = function init() {
  var self = this,
      ping,
      death;

  function resetTimeout() {
    if (death) clearTimeout(death);
    death = setTimeout(function() {
      self.reset();
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
  this.write('text', {
    type: 'text',
    text: text
  });
};

//
// ### function reset ()
// Resets state and sends close packet
//
Peer.prototype.reset = function reset() {
  this.state = 'idle';
  this.write('handshake', { type: 'clse', reason: 'reset' });
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
  // Ignore packets while initializing
  if (this.state === 'init') return;

  // Seq should be monotonic
  if (this.recSeqs[packet.group] === undefined) return;

  if (packet.group === groups.voice &&
      this.recSeqs[packet.group] + 1 !== packet.seq) {
    // Oh, we lost some voice packets - notify backend about it
    this.emit('voice', null);
  }

  if (packet.group !== groups.text && this.recSeqs[packet.group] > packet.seq) {
    return;
  }
  this.recSeqs[packet.group] = packet.seq;

  this.emit('keepalive');

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
  if (this.state !== 'idle' && this.state !== 'accepted') return this.reset();

  // Disallow non-encrypted packets
  if (!packet.public) return this.reset();

  if (!this.roomId) {
    this.roomId = packet.roomId;
  } else if (this.roomId != packet.roomId) {
    // Room id mismatch!
    return this.reset();
  }

  var fingerprint = crypto.createHash('sha1').update(packet.public)
                                             .digest('hex');

  var self = this;
  this.emit('authorize', fingerprint, function(ok) {
    clearInterval(interval);

    if (!ok) return;

    pripub.create({
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

  var interval = setInterval(function() {
    self.write('handshake', { type: 'wait' });
  }, this.waitInterval);
};

//
// ### function handleWait (packet)
// #### @packet {Object} Packet
// Handle WAIT packet
//
Peer.prototype.handleWait = function handleHello(packet) {
  if (this.state !== 'idle') return;

  clearTimeout(this._connectTimeout);
};

//
// ### function handleAccept (packet)
// #### @packet {Object} Packet
// Handle ACPT packet
//
Peer.prototype.handleAccept = function handleAccept(packet) {
  if (this.state === 'accepted') return;

  // Disallow non-encrypted packets
  if (!packet.dh) return this.reset();

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
  if (this.state !== 'accepted') return;
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
// ### function handleClose (packet)
// #### @packet {Object} packet
// Handle CLSE packet
//
Peer.prototype.handleClose = function handleClose(packet) {
  if (this.state !== 'accepted') {
    if (packet.reason !== 'reset') this.reset();
    return;
  }
  if (this.state !== 'idle') this.emit('close', 'external');

  // Reset sequence numbers
  this.resetSeqs();
};

//
// ### function close ()
// Send close packet
//
Peer.prototype.close = function close() {
  if (this.state !== 'accepted') return;
  this.state = 'idle';
  this.write('handshake', { type: 'clse' });
  this.emit('close', 'local');
};

//
// ### function connect(id, callback)
// #### @id {String } Room id
// #### @callback {Function} continuation to proceed too
// Connect to opponent
//
Peer.prototype.connect = function connect(id, callback) {
  if (this.state === 'init') {
    this._initQueue.push(function() {
      this.connect(id, callback);
    });
    return;
  }
  if (this.connecting) return;

  var self = this,
      packet = {
        type: 'helo',
        roomId: id,
        version: self.version,
        public: this.pripub.getPublicKey()
      };

  this.roomId = id;
  this.connecting = true;

  // Try handshaking multiple times
  function send() {
    self.write('handshake', packet);
  }
  var interval = setInterval(send, this.handshakeInterval);
  send();

  this._connectTimeout = setTimeout(function() {
    // Fail after timeout
    self.removeListener('connect', onConnect);
    clearInterval(interval);
    clearTimeout(self._connectTimeout);
    self.connecting = false;

    // Do not try reconnecting if we've already tried switching modes
    if (self.mode !== 'relay') {
      // Change to relay mode
      self.mode = 'relay';

      // And try reconnecting
      self.connect(id, callback);
    } else {
      self.emit('close', 'timeout');
    }
  }, this.connectTimeout);

  this.once('connect', onConnect);

  function onConnect() {
    callback && callback();

    self.removeListener('connect', onConnect);
    clearInterval(interval);
    clearTimeout(self._connectTimeout);
  }
};
