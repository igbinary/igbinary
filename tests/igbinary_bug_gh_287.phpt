--TEST--
issue when serializing/deserializing nested objects
--FILE--
<?php
class MessageEvents {
	private $events = [];

	public function add(MessageEvent $messageEvent) { $this->events[] = $messageEvent; }
	public function getEvents() { return $this->events; }
}

class MessageEvent{
	private $message;
	public function __construct(Message $message, Envelope $envelope, string $transport) {
		$this->message = $message;
	}
	public function getMessage() { return $this->message; }
}

class Envelope {
	private $message;
	public static function create(Message $message) { return new self($message); }
	public function __construct(Message $message) { $this->message = $message; }
}

class Headers extends ArrayObject{}

class Message {
	private $headers;

	public function __construct() {
		$this->headers = new Headers();
	}

	public function getHeaders(): Headers { return $this->headers; }
}

class Email extends Message {
	public function to(string $address) {
		$this->getHeaders()['To'] = $address;
		return $this;
	}
}

$messageEvents = new MessageEvents();
$messageEvents->add(new MessageEvent($message1 = (new Email())->to('alice@example.com'), Envelope::create($message1), 'null://null'));
$messageEvents->add(new MessageEvent($message2 = (new Email())->to('bob@example.com'), Envelope::create($message2), 'null://null'));

var_dump($messageEvents); // Comment/uncomment to trigger the bug

var_dump('headers_before', $messageEvents->getEvents()[0]->getMessage()->getHeaders() === $messageEvents->getEvents()[1]->getMessage()->getHeaders());

$ser = igbinary_serialize($messageEvents);

$messageEvents = igbinary_unserialize($ser);

// should dump "false", but dumps "true" the "var_dump($messageEvents)" is not commented
var_dump('headers_after', $messageEvents->getEvents()[0]->getMessage()->getHeaders() === $messageEvents->getEvents()[1]->getMessage()->getHeaders());
--EXPECT--
object(MessageEvents)#1 (1) {
  ["events":"MessageEvents":private]=>
  array(2) {
    [0]=>
    object(MessageEvent)#2 (1) {
      ["message":"MessageEvent":private]=>
      object(Email)#3 (1) {
        ["headers":"Message":private]=>
        object(Headers)#4 (1) {
          ["storage":"ArrayObject":private]=>
          array(1) {
            ["To"]=>
            string(17) "alice@example.com"
          }
        }
      }
    }
    [1]=>
    object(MessageEvent)#5 (1) {
      ["message":"MessageEvent":private]=>
      object(Email)#6 (1) {
        ["headers":"Message":private]=>
        object(Headers)#7 (1) {
          ["storage":"ArrayObject":private]=>
          array(1) {
            ["To"]=>
            string(15) "bob@example.com"
          }
        }
      }
    }
  }
}
string(14) "headers_before"
bool(false)
string(13) "headers_after"
bool(false)
