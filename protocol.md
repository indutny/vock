# Vock protocol

## Framing

All frames are JSON packed with [msgpack module](https://github.com/pgriess/node-msgpack).

```javascript
{
  "type": "hello" // frame type
  "version": [0, 1], // protocol version
  "seq": 1,       // sequence number
  ...             // other packet data
}
||
{ "enc": Encrypted buffer }
```

## Client <-> Client

Frame types:

* helo
* acpt
* voic
* ping
* pong
* clse


```
    Client1      Client2
hello        ->
  * pub = p1
            <-    hello
                     * pub = p2
            <-    accept
                     * enc(p1, DH_public = d1)
accept       ->
  * enc(p2, d2)

     BLOCK CIPHERED PART

voice        ->
            <-    voice
ping         ->
            <-    pong
clse         ->
            <-    close
```


## Client <-> Server

TODO (May be just STUN?)
