var vock = exports;

vock.cli = require('./vock/cli');

vock.audio = require('./vock/audio');
vock.socket = require('./vock/socket');
vock.api = require('./vock/api');
vock.jitter = require('./vock/jitter');

vock.instance = require('./vock/instance');
vock.peer = require('./vock/peer');

vock.version = require('../package.json').version;
