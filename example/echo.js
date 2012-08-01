var binding = require('../build/Release/vock');

var rate = 24000,
    a = new binding.Audio(rate, rate / 100);

a.ondata = function(data) {
  a.enqueue(data);
};
a.start();
