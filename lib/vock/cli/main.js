var vock = require('../../vock'),
    nconf = require('nconf'),
    fs = require('fs'),
    path = require('path'),
    colors = require('colors'),
    prompt = require('prompt'),
    keypress = require('keypress'),
    pripub = require('pripub'),
    util = require('util'),
    growl = require('growl');

var main = exports;

function Cli(argv) {
  this.argv = util._extend({}, argv);

  nconf.argv()
       .env()
       .file({ file: path.resolve(process.env.HOME, '.vock.json') });

  this.argv.keyFile = argv['key-file'] || nconf.get('key-file');

  this.cmd = argv._[0],
  this.serverHost = argv.server || argv.s || 'vock.indutny.com:43210';
  this.logger = vock.cli.utils.createLogger();
  this.known = {};

  this.initPrompt();
};

main.run = function run(argv) {
  return new Cli(argv).run();
};

Cli.prototype.initPrompt = function initPrompt() {
  var self = this;

  prompt.start();
  prompt.message = '';
  prompt.delimiter = '';
};

Cli.prototype.run = function run() {
  var self = this;

  // Show version
  if (this.argv.version) {
    self.logger.write('vock v%s', vock.version);
    process.exit(0);
  }

  self.logger.write('/// Vock - VoIP on node.js ///'.yellow);

  if (this.cmd !== 'create' && this.cmd !== 'connect' &&
      this.cmd !== 'set' && this.cmd !== 'get') {
    require('optimist').showHelp();
    process.exit(0);
  } else if (this.cmd === 'set' && this.argv._[1] && this.argv._[2]) {
    nconf.set(this.argv._[1], this.argv._[2]);
    nconf.save();
    process.exit(0);
  } else if (this.cmd === 'get' && this.argv._[1]) {
    self.logger.write('%s = %s',
                      this.argv._[1],
                      nconf.get(this.argv._[1]) || '');
    process.exit(0);
  }

  vock.cli.utils.getAddress(this.serverHost, function (err, info) {
    if (err) {
      self.logger.write('Failed to find server\'s IP address %s'.red, info);
      process.exit(1);
      return;
    }

    self.initEncryption(function(err) {
      if (err) throw err;

      self.onServerAddr(info.address, info.port);
    });
  });
};

Cli.prototype.notify = function notify(text) {
  growl(text, {
    title: 'Vock'
  });
};

Cli.prototype.prompt = function _prompt(query, callback) {
  var self = this,
      listeners = process.stdin.listeners('keypress').slice();

  process.stdin.setRawMode(false);
  process.stdin.removeAllListeners('keypress');

  this.logger.pause();

  prompt.get(query, function (err, result) {
    if (err) throw err;

    callback(result);

    // Revert all stdin/stdout changes
    self.logger.resume();
    process.stdin.resume();
    process.stdin.setRawMode(true);
    listeners.forEach(function(listener) {
      process.stdin.addListener('keypress', listener);
    });
  });
};

Cli.prototype.initEncryption = function initEncryption(callback) {
  var self = this;

  this.pripub = pripub.create({
    pri: this.argv.keyFile && fs.readFileSync(this.argv.keyFile),
    password: function(callback) {
      var property = {
        name: 'password',
        message: 'Enter your private key password:',
        hidden: true
      };

      self.prompt(property, function(result) {
        // Propagate password to instance options
        self.argv.password = result.password;

        // Continue execution
        callback(result.password);
      });
    }
  });

  this.pripub.init(callback);
};

Cli.prototype.onServerAddr = function onServerAddr(address, port) {
  var self = this;

  this.argv.server = this.server = { address: address, port: port };

  this.instance = vock.instance.create(this.argv);
  this.instance.on('error', function(err) {
    throw err;
  });

  this.instance.on('authorize', function(fingerprint, callback) {
    var property = {
      name: 'authorized',
      message: 'Accept ' + fingerprint + '?',
      validatior: /y[es]*|n[o]?/,
      warning: 'Must respond yes or no',
      default: 'no'
    };

    self.prompt(property, function(result) {
      callback(/y[es]*/.test(result.authorized));
    });
  });

  this.instance.on('peer:create', function (info) {
    self.notify('Opponent appeared ' + info.address + ':' + info.port);
    self.logger.write('Opponent appeared '.green + '%s:%d',
                      info.address,
                      info.port);
  });

  this.instance.on('peer:connect', function (info, data) {
    self.notify('Connected to opponent ' + info.address + ':' + info.port);
    self.logger.write('Connected to'.green + ' %s:%d! ' + '(mode:%s)'.white,
                      info.address,
                      info.port,
                      data.mode);
  });

  this.instance.on('peer:close', function (info, reason) {
    self.notify('Disconnected from ' + info.address + ':' + info.port);
    self.logger.write('Disconnected from'.red + ' %s:%d! ' +
                      '(reason:%s)'.white,
                      info.address,
                      info.port,
                      reason);
  });

  this.instance.on('peer:text', function (fingerprint, text) {
    self.notify(fingerprint + ': ' + text);
    self.logger.write((fingerprint + ' says: ').green + text);
  });

  this.instance.on('nat:traversal', function (how, port) {
    self.logger.write('Successfully created port mapping via %s (port %d)',
                      how,
                      port);
  });

  this.initKeyboard();

  if (this.cmd === 'create') {
    return this.handleCreate();
  } else if (this.cmd === 'connect' && this.argv._[1]) {
    return this.handleConnect();
  }

  require('optimist').showHelp();
  process.exit(0);
};

Cli.prototype.initKeyboard = function initKeyboard() {
  var self = this;

  var sendText = {
    name: 'text',
    message: 'Message text: '
  };

  // Handle keypresses
  keypress(process.stdin);
  process.stdin.on('keypress', function (ch, key) {
    if (!key) return;

    // Exit on Ctrl+C
    if (key.ctrl && key.name === 'c') {
      process.stdin.pause();
      return process.exit(0);
    }

    // Send text on N
    if (key.name === 'n') {
      self.prompt(sendText, function(result) {
        // Ignore empty sends
        if (!result.text) return;

        self.instance.sendText(result.text);
      });
      return;
    }

    // Mute on M
    if (key.name === 'm') {
      var res = self.instance.toggleMute();
      self.logger.write('Mute: ' + (res ? 'enabled' : 'disabled').black);
      return;
    }
  });
  process.stdin.setRawMode(true);
  process.stdin.resume();
};

Cli.prototype.handleCreate = function handleCreate() {
  var self = this;

  this.instance.api.create(function(err, id) {
    if (err) throw err;

    self.logger.write('Room created!'.green);
    self.logger.write('Run this on other side:');
    self.logger.write('  vock connect ' + id);
    self.logger.write('Waiting for opponent...');

    self.instance.api.watch(id, self.handleMembers.bind(self));
  });
};

Cli.prototype.handleConnect = function handleConnect() {
  var self = this,
      id = this.argv._[1];


  this.instance.api.connect(id, this.handleMembers.bind(this));
  this.logger.write('Connecting...');
};

Cli.prototype.handleMembers = function handleMembers(err, info) {
  if (err) {
    console.error(err.stack);
    return;
  }

  var self = this;

  info.members.forEach(function(target) {
    var id = self.instance.getId(target);
    if (self.known.hasOwnProperty(id)) return;
    self.known[id] = true;

    self.instance.connect(info.id, target.port, target.address);
  });
};
