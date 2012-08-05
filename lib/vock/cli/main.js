var vock = require('../../vock'),
    colors = require('colors'),
    prompt = require('prompt');

var main = exports;

function Cli(argv) {
  this.argv = argv;
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
  prompt.start();
  prompt.message = '';
  prompt.delimiter = '';
};

Cli.prototype.run = function run() {
  var self = this;

  // Show version
  if (this.argv.version) {
    console.log('vock v%s', vock.version);
    process.exit(0);
  }

  vock.cli.utils.getAddress(this.serverHost, function (err, info) {
    if (err) {
      self.logger.write('Failed to find server\'s IP address %s'.red, info);
      process.exit(1);
      return;
    }

    self.onServerAddr(info.address, info.port);
  });
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

    self.logger.pause();
    prompt.get(property, function (err, result) {
      if (err) throw err;

      callback(/y[es]*/.test(result.authorized));
      self.logger.resume();
    });
  });

  this.instance.on('peer:create', function (info) {
    self.logger.write('Opponent appeared '.green + '%s:%d',
                      info.address,
                      info.port);
  });

  this.instance.on('peer:connect', function (info, data) {
    self.logger.write('Connected to'.green + ' %s:%d! ' + '(mode:%s)'.white,
                      info.address,
                      info.port,
                      data.mode);
  });

  if (this.cmd === 'create') {
    return this.handleCreate();
  } else if (this.cmd === 'connect' && this.argv._[1]) {
    return this.handleConnect();
  }

  require('optimist').showHelp()
  process.exit(0);
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
  return;
};

Cli.prototype.handleConnect = function handleConnect() {
  var self = this,
      id = this.argv._[1];

  this.instance.api.connect(id, this.handleMembers.bind(this));
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
