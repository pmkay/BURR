// BURR — PebbleKit JS entry point.
//
// All configuration is handled on-device by Clay: it builds the settings page
// locally (no web server, works offline) and automatically wires up the
// `showConfiguration` / `webviewclosed` events, sending the chosen values to
// the watch via AppMessage. The settings schema lives in ./config.json.
var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);
