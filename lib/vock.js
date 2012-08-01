var vock = exports;

vock.audio = require('./vock/audio');
vock.socket = require('./vock/socket');
vock.api = require('./vock/api');
vock.protocol = require('./vock/protocol');
vock.jitter = require('./vock/jitter');
vock.instance = require('./vock/instance');

vock.version = require('../package.json').version;
