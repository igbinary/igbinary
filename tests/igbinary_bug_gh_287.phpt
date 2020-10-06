--TEST--
issue when serializing/deserializing nested objects
--FILE--
<?php
/**
 * Event is the base class for classes containing event data.
 *
 * This class contains no event data. It is used by events that do not pass
 * state information to an event handler when an event is raised.
 *
 * You can call the method stopPropagation() to abort the execution of
 * further listeners in your event listener.
 *
 * @author Guilherme Blanco <guilhermeblanco@hotmail.com>
 * @author Jonathan Wage <jonwage@gmail.com>
 * @author Roman Borschel <roman@code-factory.org>
 * @author Bernhard Schussek <bschussek@gmail.com>
 * @author Nicolas Grekas <p@tchwork.com>
 */
class Event
{
    private $propagationStopped = false;

    /**
     * {@inheritdoc}
     */
    public function isPropagationStopped(): bool
    {
        return $this->propagationStopped;
    }

    /**
     * Stops the propagation of the event to further event listeners.
     *
     * If multiple event listeners are connected to the same event, no
     * further event listener will be triggered once any trigger calls
     * stopPropagation().
     */
    public function stopPropagation(): void
    {
        $this->propagationStopped = true;
    }
}

/**
 * @author Fabien Potencier <fabien@symfony.com>
 */
class MessageEvents
{
    private $events = [];
    private $transports = [];

    public function add(MessageEvent $event): void
    {
        $this->events[] = $event;
        $this->transports[$event->getTransport()] = true;
    }

    public function getTransports(): array
    {
        return array_keys($this->transports);
    }

    /**
     * @return MessageEvent[]
     */
    public function getEvents(string $name = null): array
    {
        if (null === $name) {
            return $this->events;
        }

        $events = [];
        foreach ($this->events as $event) {
            if ($name === $event->getTransport()) {
                $events[] = $event;
            }
        }

        return $events;
    }

    /**
     * @return RawMessage[]
     */
    public function getMessages(string $name = null): array
    {
        $events = $this->getEvents($name);
        $messages = [];
        foreach ($events as $event) {
            $messages[] = $event->getMessage();
        }

        return $messages;
    }
}

/**
 * Allows the transformation of a Message and the Envelope before the email is sent.
 *
 * @author Fabien Potencier <fabien@symfony.com>
 */
final class MessageEvent extends Event
{
    private $message;
    private $envelope;
    private $transport;
    private $queued;

    public function __construct(RawMessage $message, Envelope $envelope, string $transport, bool $queued = false)
    {
        $this->message = $message;
        $this->envelope = $envelope;
        $this->transport = $transport;
        $this->queued = $queued;
    }

    public function getMessage(): RawMessage
    {
        return $this->message;
    }

    public function setMessage(RawMessage $message): void
    {
        $this->message = $message;
    }

    public function getEnvelope(): Envelope
    {
        return $this->envelope;
    }

    public function setEnvelope(Envelope $envelope): void
    {
        $this->envelope = $envelope;
    }

    public function getTransport(): string
    {
        return $this->transport;
    }

    public function isQueued(): bool
    {
        return $this->queued;
    }
}

/**
 * @author Fabien Potencier <fabien@symfony.com>
 */
class Envelope
{
    private $sender;
    private $recipients = [];

    /**
     * @param Address[] $recipients
     */
    public function __construct(Address $sender, array $recipients)
    {
        $this->setSender($sender);
        $this->setRecipients($recipients);
    }

    public static function create(RawMessage $message): self
    {
        if (RawMessage::class === \get_class($message)) {
            throw new LogicException('Cannot send a RawMessage instance without an explicit Envelope.');
        }

        return new DelayedEnvelope($message);
    }

    public function setSender(Address $sender): void
    {
        // to ensure deliverability of bounce emails independent of UTF-8 capabilities of SMTP servers
        if (!preg_match('/^[^@\x80-\xFF]++@/', $sender->getAddress())) {
            throw new InvalidArgumentException(sprintf('Invalid sender "%s": non-ASCII characters not supported in local-part of email.', $sender->getAddress()));
        }
        $this->sender = $sender;
    }

    /**
     * @return Address Returns a "mailbox" as specified by RFC 2822
     *                 Must be converted to an "addr-spec" when used as a "MAIL FROM" value in SMTP (use getAddress())
     */
    public function getSender(): Address
    {
        return $this->sender;
    }

    /**
     * @param Address[] $recipients
     */
    public function setRecipients(array $recipients): void
    {
        if (!$recipients) {
            throw new InvalidArgumentException('An envelope must have at least one recipient.');
        }

        $this->recipients = [];
        foreach ($recipients as $recipient) {
            if (!$recipient instanceof Address) {
                throw new InvalidArgumentException(sprintf('A recipient must be an instance of "%s" (got "%s").', Address::class, get_debug_type($recipient)));
            }
            $this->recipients[] = new Address($recipient->getAddress());
        }
    }

    /**
     * @return Address[]
     */
    public function getRecipients(): array
    {
        return $this->recipients;
    }
}

/**
 * @author Fabien Potencier <fabien@symfony.com>
 *
 * @internal
 */
final class DelayedEnvelope extends Envelope
{
    private $senderSet = false;
    private $recipientsSet = false;
    private $message;

    public function __construct(Message $message)
    {
        $this->message = $message;
    }

    public function setSender(Address $sender): void
    {
        parent::setSender($sender);

        $this->senderSet = true;
    }

    public function getSender(): Address
    {
        if (!$this->senderSet) {
            parent::setSender(self::getSenderFromHeaders($this->message->getHeaders()));
        }

        return parent::getSender();
    }

    public function setRecipients(array $recipients): void
    {
        parent::setRecipients($recipients);

        $this->recipientsSet = parent::getRecipients();
    }

    /**
     * @return Address[]
     */
    public function getRecipients(): array
    {
        if ($this->recipientsSet) {
            return parent::getRecipients();
        }

        return self::getRecipientsFromHeaders($this->message->getHeaders());
    }

    private static function getRecipientsFromHeaders(Headers $headers): array
    {
        $recipients = [];
        foreach (['to', 'cc', 'bcc'] as $name) {
            foreach ($headers->all($name) as $header) {
                foreach ($header->getAddresses() as $address) {
                    $recipients[] = $address;
                }
            }
        }

        return $recipients;
    }

    private static function getSenderFromHeaders(Headers $headers): Address
    {
        if ($sender = $headers->get('Sender')) {
            return $sender->getAddress();
        }
        if ($from = $headers->get('From')) {
            return $from->getAddresses()[0];
        }
        if ($return = $headers->get('Return-Path')) {
            return $return->getAddress();
        }

        throw new LogicException('Unable to determine the sender of the message.');
    }
}

final class Address
{
    /**
     * A regex that matches a structure like 'Name <email@address.com>'.
     * It matches anything between the first < and last > as email address.
     * This allows to use a single string to construct an Address, which can be convenient to use in
     * config, and allows to have more readable config.
     * This does not try to cover all edge cases for address.
     */
    private const FROM_STRING_PATTERN = '~(?<displayName>[^<]*)<(?<addrSpec>.*)>[^>]*~';

    private $address;
    private $name;

    public function __construct(string $address, string $name = '')
    {
        $this->address = trim($address);
        $this->name = trim(str_replace(["\n", "\r"], '', $name));
    }

    /**
     * @param Address|string $address
     */
    public static function create($address): self
    {
        if ($address instanceof self) {
            return $address;
        }
        if (\is_string($address)) {
            if (false === strpos($address, '<')) {
                return new self($address);
            }

            if (!preg_match(self::FROM_STRING_PATTERN, $address, $matches)) {
                throw new InvalidArgumentException(sprintf('Could not parse "%s" to a "%s" instance.', $address, self::class));
            }

            return new self($matches['addrSpec'], trim($matches['displayName'], ' \'"'));
        }

        throw new InvalidArgumentException(sprintf('An address can be an instance of Address or a string ("%s" given).', get_debug_type($address)));
    }

    /**
     * @param (Address|string)[] $addresses
     *
     * @return Address[]
     */
    public static function createArray(array $addresses): array
    {
        $addrs = [];
        foreach ($addresses as $address) {
            $addrs[] = self::create($address);
        }

        return $addrs;
    }
}

/**
 * A MIME Header.
 *
 * @author Chris Corbyn
 */
interface HeaderInterface
{
    /**
     * Sets the body.
     *
     * The type depends on the Header concrete class.
     *
     * @param mixed $body
     */
    public function setBody($body);

    /**
     * Gets the body.
     *
     * The return type depends on the Header concrete class.
     *
     * @return mixed
     */
    public function getBody();

    public function setCharset(string $charset);

    public function getCharset(): ?string;

    public function setLanguage(string $lang);

    public function getLanguage(): ?string;

    public function getName(): string;

    public function setMaxLineLength(int $lineLength);

    public function getMaxLineLength(): int;

    /**
     * Gets this Header rendered as a compliant string.
     */
    public function toString(): string;

    /**
     * Gets the header's body, prepared for folding into a final header value.
     *
     * This is not necessarily RFC 2822 compliant since folding white space is
     * not added at this stage (see {@link toString()} for that).
     */
    public function getBodyAsString(): string;
}


/**
 * An abstract base MIME Header.
 *
 * @author Chris Corbyn
 */
abstract class AbstractHeader implements HeaderInterface
{
    const PHRASE_PATTERN = '(?:(?:(?:(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))*(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))|(?:(?:[ \t]*(?:\r\n))?[ \t])))?[a-zA-Z0-9!#\$%&\'\*\+\-\/=\?\^_`\{\}\|~]+(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))*(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))|(?:(?:[ \t]*(?:\r\n))?[ \t])))?)|(?:(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))*(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))|(?:(?:[ \t]*(?:\r\n))?[ \t])))?"((?:(?:[ \t]*(?:\r\n))?[ \t])?(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21\x23-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])))*(?:(?:[ \t]*(?:\r\n))?[ \t])?"(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))*(?:(?:(?:(?:[ \t]*(?:\r\n))?[ \t])?(\((?:(?:(?:[ \t]*(?:\r\n))?[ \t])|(?:(?:[\x01-\x08\x0B\x0C\x0E-\x19\x7F]|[\x21-\x27\x2A-\x5B\x5D-\x7E])|(?:\\[\x00-\x08\x0B\x0C\x0E-\x7F])|(?1)))*(?:(?:[ \t]*(?:\r\n))?[ \t])?\)))|(?:(?:[ \t]*(?:\r\n))?[ \t])))?))+?)';

    private static $encoder;

    private $name;
    private $lineLength = 76;
    private $lang;
    private $charset = 'utf-8';

    public function __construct(string $name)
    {
        $this->name = $name;
    }

    public function setCharset(string $charset)
    {
        $this->charset = $charset;
    }

    public function getCharset(): ?string
    {
        return $this->charset;
    }

    /**
     * Set the language used in this Header.
     *
     * For example, for US English, 'en-us'.
     */
    public function setLanguage(string $lang)
    {
        $this->lang = $lang;
    }

    public function getLanguage(): ?string
    {
        return $this->lang;
    }

    public function getName(): string
    {
        return $this->name;
    }

    public function setMaxLineLength(int $lineLength)
    {
        $this->lineLength = $lineLength;
    }

    public function getMaxLineLength(): int
    {
        return $this->lineLength;
    }

    public function toString(): string
    {
        return $this->tokensToString($this->toTokens());
    }

    /**
     * Produces a compliant, formatted RFC 2822 'phrase' based on the string given.
     *
     * @param string $string  as displayed
     * @param bool   $shorten the first line to make remove for header name
     */
    protected function createPhrase(HeaderInterface $header, string $string, string $charset, bool $shorten = false): string
    {
        // Treat token as exactly what was given
        $phraseStr = $string;

        // If it's not valid
        if (!preg_match('/^'.self::PHRASE_PATTERN.'$/D', $phraseStr)) {
            // .. but it is just ascii text, try escaping some characters
            // and make it a quoted-string
            if (preg_match('/^[\x00-\x08\x0B\x0C\x0E-\x7F]*$/D', $phraseStr)) {
                foreach (['\\', '"'] as $char) {
                    $phraseStr = str_replace($char, '\\'.$char, $phraseStr);
                }
                $phraseStr = '"'.$phraseStr.'"';
            } else {
                // ... otherwise it needs encoding
                // Determine space remaining on line if first line
                if ($shorten) {
                    $usedLength = \strlen($header->getName().': ');
                } else {
                    $usedLength = 0;
                }
                $phraseStr = $this->encodeWords($header, $string, $usedLength);
            }
        }

        return $phraseStr;
    }

    /**
     * Encode needed word tokens within a string of input.
     */
    protected function encodeWords(HeaderInterface $header, string $input, int $usedLength = -1): string
    {
        $value = '';
        $tokens = $this->getEncodableWordTokens($input);
        foreach ($tokens as $token) {
            // See RFC 2822, Sect 2.2 (really 2.2 ??)
            if ($this->tokenNeedsEncoding($token)) {
                // Don't encode starting WSP
                $firstChar = substr($token, 0, 1);
                switch ($firstChar) {
                    case ' ':
                    case "\t":
                        $value .= $firstChar;
                        $token = substr($token, 1);
                }

                if (-1 == $usedLength) {
                    $usedLength = \strlen($header->getName().': ') + \strlen($value);
                }
                $value .= $this->getTokenAsEncodedWord($token, $usedLength);
            } else {
                $value .= $token;
            }
        }

        return $value;
    }

    protected function tokenNeedsEncoding(string $token): bool
    {
        return (bool) preg_match('~[\x00-\x08\x10-\x19\x7F-\xFF\r\n]~', $token);
    }

    /**
     * Splits a string into tokens in blocks of words which can be encoded quickly.
     *
     * @return string[]
     */
    protected function getEncodableWordTokens(string $string): array
    {
        $tokens = [];
        $encodedToken = '';
        // Split at all whitespace boundaries
        foreach (preg_split('~(?=[\t ])~', $string) as $token) {
            if ($this->tokenNeedsEncoding($token)) {
                $encodedToken .= $token;
            } else {
                if (\strlen($encodedToken) > 0) {
                    $tokens[] = $encodedToken;
                    $encodedToken = '';
                }
                $tokens[] = $token;
            }
        }
        if (\strlen($encodedToken)) {
            $tokens[] = $encodedToken;
        }

        return $tokens;
    }

    /**
     * Get a token as an encoded word for safe insertion into headers.
     */
    protected function getTokenAsEncodedWord(string $token, int $firstLineOffset = 0): string
    {
        if (null === self::$encoder) {
            self::$encoder = new QpMimeHeaderEncoder();
        }

        // Adjust $firstLineOffset to account for space needed for syntax
        $charsetDecl = $this->charset;
        if (null !== $this->lang) {
            $charsetDecl .= '*'.$this->lang;
        }
        $encodingWrapperLength = \strlen('=?'.$charsetDecl.'?'.self::$encoder->getName().'??=');

        if ($firstLineOffset >= 75) {
            //Does this logic need to be here?
            $firstLineOffset = 0;
        }

        $encodedTextLines = explode("\r\n",
            self::$encoder->encodeString($token, $this->charset, $firstLineOffset, 75 - $encodingWrapperLength)
        );

        if ('iso-2022-jp' !== strtolower($this->charset)) {
            // special encoding for iso-2022-jp using mb_encode_mimeheader
            foreach ($encodedTextLines as $lineNum => $line) {
                $encodedTextLines[$lineNum] = '=?'.$charsetDecl.'?'.self::$encoder->getName().'?'.$line.'?=';
            }
        }

        return implode("\r\n ", $encodedTextLines);
    }

    /**
     * Generates tokens from the given string which include CRLF as individual tokens.
     *
     * @return string[]
     */
    protected function generateTokenLines(string $token): array
    {
        return preg_split('~(\r\n)~', $token, -1, \PREG_SPLIT_DELIM_CAPTURE);
    }

    /**
     * Generate a list of all tokens in the final header.
     */
    protected function toTokens(string $string = null): array
    {
        if (null === $string) {
            $string = $this->getBodyAsString();
        }

        $tokens = [];
        // Generate atoms; split at all invisible boundaries followed by WSP
        foreach (preg_split('~(?=[ \t])~', $string) as $token) {
            $newTokens = $this->generateTokenLines($token);
            foreach ($newTokens as $newToken) {
                $tokens[] = $newToken;
            }
        }

        return $tokens;
    }

    /**
     * Takes an array of tokens which appear in the header and turns them into
     * an RFC 2822 compliant string, adding FWSP where needed.
     *
     * @param string[] $tokens
     */
    private function tokensToString(array $tokens): string
    {
        $lineCount = 0;
        $headerLines = [];
        $headerLines[] = $this->name.': ';
        $currentLine = &$headerLines[$lineCount++];

        // Build all tokens back into compliant header
        foreach ($tokens as $i => $token) {
            // Line longer than specified maximum or token was just a new line
            if (("\r\n" === $token) ||
                ($i > 0 && \strlen($currentLine.$token) > $this->lineLength)
                && 0 < \strlen($currentLine)) {
                $headerLines[] = '';
                $currentLine = &$headerLines[$lineCount++];
            }

            // Append token to the line
            if ("\r\n" !== $token) {
                $currentLine .= $token;
            }
        }

        // Implode with FWS (RFC 2822, 2.2.3)
        return implode("\r\n", $headerLines);
    }
}

/**
 * A collection of headers.
 *
 * @author Fabien Potencier <fabien@symfony.com>
 */
final class Headers
{
    private const UNIQUE_HEADERS = [
        'date', 'from', 'sender', 'reply-to', 'to', 'cc', 'bcc',
        'message-id', 'in-reply-to', 'references', 'subject',
    ];
    private const HEADER_CLASS_MAP = [
        'date' => DateHeader::class,
        'from' => MailboxListHeader::class,
        'sender' => MailboxHeader::class,
        'reply-to' => MailboxListHeader::class,
        'to' => MailboxListHeader::class,
        'cc' => MailboxListHeader::class,
        'bcc' => MailboxListHeader::class,
        'message-id' => IdentificationHeader::class,
        'in-reply-to' => IdentificationHeader::class,
        'references' => IdentificationHeader::class,
        'return-path' => PathHeader::class,
    ];

    /**
     * @var HeaderInterface[][]
     */
    private $headers = [];
    private $lineLength = 76;

    public function __construct(HeaderInterface ...$headers)
    {
        foreach ($headers as $header) {
            $this->add($header);
        }
    }

    public function __clone()
    {
        foreach ($this->headers as $name => $collection) {
            foreach ($collection as $i => $header) {
                $this->headers[$name][$i] = clone $header;
            }
        }
    }

    public function setMaxLineLength(int $lineLength)
    {
        $this->lineLength = $lineLength;
        foreach ($this->all() as $header) {
            $header->setMaxLineLength($lineLength);
        }
    }

    public function getMaxLineLength(): int
    {
        return $this->lineLength;
    }

    /**
     * @param (Address|string)[] $addresses
     *
     * @return $this
     */
    public function addMailboxListHeader(string $name, array $addresses): self
    {
        return $this->add(new MailboxListHeader($name, Address::createArray($addresses)));
    }

    /**
     * @param Address|string $address
     *
     * @return $this
     */
    public function addMailboxHeader(string $name, $address): self
    {
        return $this->add(new MailboxHeader($name, Address::create($address)));
    }

    /**
     * @param string|array $ids
     *
     * @return $this
     */
    public function addIdHeader(string $name, $ids): self
    {
        return $this->add(new IdentificationHeader($name, $ids));
    }

    /**
     * @param Address|string $path
     *
     * @return $this
     */
    public function addPathHeader(string $name, $path): self
    {
        return $this->add(new PathHeader($name, $path instanceof Address ? $path : new Address($path)));
    }

    /**
     * @return $this
     */
    public function addDateHeader(string $name, \DateTimeInterface $dateTime): self
    {
        return $this->add(new DateHeader($name, $dateTime));
    }

    /**
     * @return $this
     */
    public function addTextHeader(string $name, string $value): self
    {
        return $this->add(new UnstructuredHeader($name, $value));
    }

    /**
     * @return $this
     */
    public function addParameterizedHeader(string $name, string $value, array $params = []): self
    {
        return $this->add(new ParameterizedHeader($name, $value, $params));
    }

    /**
     * @return $this
     */
    public function addHeader(string $name, $argument, array $more = []): self
    {
        $parts = explode('\\', self::HEADER_CLASS_MAP[$name] ?? UnstructuredHeader::class);
        $method = 'add'.ucfirst(array_pop($parts));
        if ('addUnstructuredHeader' === $method) {
            $method = 'addTextHeader';
        } elseif ('addIdentificationHeader' === $method) {
            $method = 'addIdHeader';
        }

        return $this->$method($name, $argument, $more);
    }

    public function has(string $name): bool
    {
        return isset($this->headers[strtolower($name)]);
    }

    /**
     * @return $this
     */
    public function add(HeaderInterface $header): self
    {
        self::checkHeaderClass($header);

        $header->setMaxLineLength($this->lineLength);
        $name = strtolower($header->getName());

        if (\in_array($name, self::UNIQUE_HEADERS, true) && isset($this->headers[$name]) && \count($this->headers[$name]) > 0) {
            throw new LogicException(sprintf('Impossible to set header "%s" as it\'s already defined and must be unique.', $header->getName()));
        }

        $this->headers[$name][] = $header;

        return $this;
    }

    public function get(string $name): ?HeaderInterface
    {
        $name = strtolower($name);
        if (!isset($this->headers[$name])) {
            return null;
        }

        $values = array_values($this->headers[$name]);

        return array_shift($values);
    }

    public function all(string $name = null): iterable
    {
        if (null === $name) {
            foreach ($this->headers as $name => $collection) {
                foreach ($collection as $header) {
                    yield $name => $header;
                }
            }
        } elseif (isset($this->headers[strtolower($name)])) {
            foreach ($this->headers[strtolower($name)] as $header) {
                yield $header;
            }
        }
    }

    public function getNames(): array
    {
        return array_keys($this->headers);
    }

    public function remove(string $name): void
    {
        unset($this->headers[strtolower($name)]);
    }

    public static function isUniqueHeader(string $name): bool
    {
        return \in_array($name, self::UNIQUE_HEADERS, true);
    }

    /**
     * @throws LogicException if the header name and class are not compatible
     */
    public static function checkHeaderClass(HeaderInterface $header): void
    {
        $name = strtolower($header->getName());

        if (($c = self::HEADER_CLASS_MAP[$name] ?? null) && !$header instanceof $c) {
            throw new LogicException(sprintf('The "%s" header must be an instance of "%s" (got "%s").', $header->getName(), $c, get_debug_type($header)));
        }
    }

    public function toString(): string
    {
        $string = '';
        foreach ($this->toArray() as $str) {
            $string .= $str."\r\n";
        }

        return $string;
    }

    public function toArray(): array
    {
        $arr = [];
        foreach ($this->all() as $header) {
            if ('' !== $header->getBodyAsString()) {
                $arr[] = $header->toString();
            }
        }

        return $arr;
    }

    /**
     * @internal
     */
    public function getHeaderBody($name)
    {
        return $this->has($name) ? $this->get($name)->getBody() : null;
    }

    /**
     * @internal
     */
    public function setHeaderBody(string $type, string $name, $body): void
    {
        if ($this->has($name)) {
            $this->get($name)->setBody($body);
        } else {
            $this->{'add'.$type.'Header'}($name, $body);
        }
    }

    /**
     * @internal
     */
    public function getHeaderParameter(string $name, string $parameter): ?string
    {
        if (!$this->has($name)) {
            return null;
        }

        $header = $this->get($name);
        if (!$header instanceof ParameterizedHeader) {
            throw new LogicException(sprintf('Unable to get parameter "%s" on header "%s" as the header is not of class "%s".', $parameter, $name, ParameterizedHeader::class));
        }

        return $header->getParameter($parameter);
    }

    /**
     * @internal
     */
    public function setHeaderParameter(string $name, string $parameter, $value): void
    {
        if (!$this->has($name)) {
            throw new LogicException(sprintf('Unable to set parameter "%s" on header "%s" as the header is not defined.', $parameter, $name));
        }

        $header = $this->get($name);
        if (!$header instanceof ParameterizedHeader) {
            throw new LogicException(sprintf('Unable to set parameter "%s" on header "%s" as the header is not of class "%s".', $parameter, $name, ParameterizedHeader::class));
        }

        $header->setParameter($parameter, $value);
    }
}

/**
 * A Mailbox list MIME Header for something like From, To, Cc, and Bcc (one or more named addresses).
 *
 * @author Chris Corbyn
 */
final class MailboxListHeader extends AbstractHeader
{
    private $addresses = [];

    /**
     * @param Address[] $addresses
     */
    public function __construct(string $name, array $addresses)
    {
        parent::__construct($name);

        $this->setAddresses($addresses);
    }

    /**
     * @param Address[] $body
     *
     * @throws RfcComplianceException
     */
    public function setBody($body)
    {
        $this->setAddresses($body);
    }

    /**
     * @throws RfcComplianceException
     *
     * @return Address[]
     */
    public function getBody(): array
    {
        return $this->getAddresses();
    }

    /**
     * Sets a list of addresses to be shown in this Header.
     *
     * @param Address[] $addresses
     *
     * @throws RfcComplianceException
     */
    public function setAddresses(array $addresses)
    {
        $this->addresses = [];
        $this->addAddresses($addresses);
    }

    /**
     * Sets a list of addresses to be shown in this Header.
     *
     * @param Address[] $addresses
     *
     * @throws RfcComplianceException
     */
    public function addAddresses(array $addresses)
    {
        foreach ($addresses as $address) {
            $this->addAddress($address);
        }
    }

    /**
     * @throws RfcComplianceException
     */
    public function addAddress(Address $address)
    {
        $this->addresses[] = $address;
    }

    /**
     * @return Address[]
     */
    public function getAddresses(): array
    {
        return $this->addresses;
    }

    /**
     * Gets the full mailbox list of this Header as an array of valid RFC 2822 strings.
     *
     * @throws RfcComplianceException
     *
     * @return string[]
     */
    public function getAddressStrings(): array
    {
        $strings = [];
        foreach ($this->addresses as $address) {
            $str = $address->getEncodedAddress();
            if ($name = $address->getName()) {
                $str = $this->createPhrase($this, $name, $this->getCharset(), !$strings).' <'.$str.'>';
            }
            $strings[] = $str;
        }

        return $strings;
    }

    public function getBodyAsString(): string
    {
        return implode(', ', $this->getAddressStrings());
    }

    /**
     * Redefine the encoding requirements for addresses.
     *
     * All "specials" must be encoded as the full header value will not be quoted
     *
     * @see RFC 2822 3.2.1
     */
    protected function tokenNeedsEncoding(string $token): bool
    {
        return preg_match('/[()<>\[\]:;@\,."]/', $token) || parent::tokenNeedsEncoding($token);
    }
}

class RawMessage implements \Serializable
{
    private $message;

    public function __construct($message)
    {
        $this->message = $message;
    }

    final public function serialize(): string
    {
        return serialize($this->__serialize());
    }

    /**
     * @internal
     */
    final public function unserialize($serialized)
    {
        $this->__unserialize(unserialize($serialized));
    }

    public function __serialize(): array
    {
        return [$this->message];
    }

    public function __unserialize(array $data): void
    {
        [$this->message] = $data;
    }
}

abstract class AbstractPart
{
    private $headers;

    public function __construct()
    {
        $this->headers = new Headers();
    }

    public function getHeaders(): Headers
    {
        return $this->headers;
    }
}

class Message extends RawMessage
{
    private $headers;
    private $body;

    public function __construct(Headers $headers = null, AbstractPart $body = null)
    {
        $this->headers = $headers ? clone $headers : new Headers();
        $this->body = $body;
    }

    public function __clone()
    {
        $this->headers = clone $this->headers;

        if (null !== $this->body) {
            $this->body = clone $this->body;
        }
    }

	public function getHeaders(): Headers { return $this->headers; }

    public function __serialize(): array
    {
        return [$this->headers];
    }

    public function __unserialize(array $data): void
    {
        [$this->headers] = $data;
    }
}


/**
 * @author Fabien Potencier <fabien@symfony.com>
 */
class Email extends Message
{
    const PRIORITY_HIGHEST = 1;
    const PRIORITY_HIGH = 2;
    const PRIORITY_NORMAL = 3;
    const PRIORITY_LOW = 4;
    const PRIORITY_LOWEST = 5;

    private const PRIORITY_MAP = [
        self::PRIORITY_HIGHEST => 'Highest',
        self::PRIORITY_HIGH => 'High',
        self::PRIORITY_NORMAL => 'Normal',
        self::PRIORITY_LOW => 'Low',
        self::PRIORITY_LOWEST => 'Lowest',
    ];

    private $text;
    private $textCharset;
    private $html;
    private $htmlCharset;
    private $attachments = [];

    /**
     * @return $this
     */
    public function subject(string $subject)
    {
        return $this->setHeaderBody('Text', 'Subject', $subject);
    }

    public function getSubject(): ?string
    {
        return $this->getHeaders()->getHeaderBody('Subject');
    }

    /**
     * @return $this
     */
    public function date(\DateTimeInterface $dateTime)
    {
        return $this->setHeaderBody('Date', 'Date', $dateTime);
    }

    public function getDate(): ?\DateTimeImmutable
    {
        return $this->getHeaders()->getHeaderBody('Date');
    }

    /**
     * @param Address|string $address
     *
     * @return $this
     */
    public function returnPath($address)
    {
        return $this->setHeaderBody('Path', 'Return-Path', Address::create($address));
    }

    public function getReturnPath(): ?Address
    {
        return $this->getHeaders()->getHeaderBody('Return-Path');
    }

    /**
     * @param Address|string $address
     *
     * @return $this
     */
    public function sender($address)
    {
        return $this->setHeaderBody('Mailbox', 'Sender', Address::create($address));
    }

    public function getSender(): ?Address
    {
        return $this->getHeaders()->getHeaderBody('Sender');
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function addFrom(...$addresses)
    {
        return $this->addListAddressHeaderBody('From', $addresses);
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function from(...$addresses)
    {
        return $this->setListAddressHeaderBody('From', $addresses);
    }

    /**
     * @return Address[]
     */
    public function getFrom(): array
    {
        return $this->getHeaders()->getHeaderBody('From') ?: [];
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function addReplyTo(...$addresses)
    {
        return $this->addListAddressHeaderBody('Reply-To', $addresses);
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function replyTo(...$addresses)
    {
        return $this->setListAddressHeaderBody('Reply-To', $addresses);
    }

    /**
     * @return Address[]
     */
    public function getReplyTo(): array
    {
        return $this->getHeaders()->getHeaderBody('Reply-To') ?: [];
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function addTo(...$addresses)
    {
        return $this->addListAddressHeaderBody('To', $addresses);
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function to(...$addresses)
    {
        return $this->setListAddressHeaderBody('To', $addresses);
    }

    /**
     * @return Address[]
     */
    public function getTo(): array
    {
        return $this->getHeaders()->getHeaderBody('To') ?: [];
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function addCc(...$addresses)
    {
        return $this->addListAddressHeaderBody('Cc', $addresses);
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function cc(...$addresses)
    {
        return $this->setListAddressHeaderBody('Cc', $addresses);
    }

    /**
     * @return Address[]
     */
    public function getCc(): array
    {
        return $this->getHeaders()->getHeaderBody('Cc') ?: [];
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function addBcc(...$addresses)
    {
        return $this->addListAddressHeaderBody('Bcc', $addresses);
    }

    /**
     * @param Address|string ...$addresses
     *
     * @return $this
     */
    public function bcc(...$addresses)
    {
        return $this->setListAddressHeaderBody('Bcc', $addresses);
    }

    /**
     * @return Address[]
     */
    public function getBcc(): array
    {
        return $this->getHeaders()->getHeaderBody('Bcc') ?: [];
    }

    /**
     * Sets the priority of this message.
     *
     * The value is an integer where 1 is the highest priority and 5 is the lowest.
     *
     * @return $this
     */
    public function priority(int $priority)
    {
        if ($priority > 5) {
            $priority = 5;
        } elseif ($priority < 1) {
            $priority = 1;
        }

        return $this->setHeaderBody('Text', 'X-Priority', sprintf('%d (%s)', $priority, self::PRIORITY_MAP[$priority]));
    }

    /**
     * Get the priority of this message.
     *
     * The returned value is an integer where 1 is the highest priority and 5
     * is the lowest.
     */
    public function getPriority(): int
    {
        list($priority) = sscanf($this->getHeaders()->getHeaderBody('X-Priority'), '%[1-5]');

        return $priority ?? 3;
    }

    /**
     * @param resource|string $body
     *
     * @return $this
     */
    public function text($body, string $charset = 'utf-8')
    {
        $this->text = $body;
        $this->textCharset = $charset;

        return $this;
    }

    /**
     * @return resource|string|null
     */
    public function getTextBody()
    {
        return $this->text;
    }

    public function getTextCharset(): ?string
    {
        return $this->textCharset;
    }

    /**
     * @param resource|string|null $body
     *
     * @return $this
     */
    public function html($body, string $charset = 'utf-8')
    {
        $this->html = $body;
        $this->htmlCharset = $charset;

        return $this;
    }

    /**
     * @return resource|string|null
     */
    public function getHtmlBody()
    {
        return $this->html;
    }

    public function getHtmlCharset(): ?string
    {
        return $this->htmlCharset;
    }

    /**
     * @param resource|string $body
     *
     * @return $this
     */
    public function attach($body, string $name = null, string $contentType = null)
    {
        $this->attachments[] = ['body' => $body, 'name' => $name, 'content-type' => $contentType, 'inline' => false];

        return $this;
    }

    /**
     * @return $this
     */
    public function attachFromPath(string $path, string $name = null, string $contentType = null)
    {
        $this->attachments[] = ['path' => $path, 'name' => $name, 'content-type' => $contentType, 'inline' => false];

        return $this;
    }

    /**
     * @param resource|string $body
     *
     * @return $this
     */
    public function embed($body, string $name = null, string $contentType = null)
    {
        $this->attachments[] = ['body' => $body, 'name' => $name, 'content-type' => $contentType, 'inline' => true];

        return $this;
    }

    /**
     * @return $this
     */
    public function embedFromPath(string $path, string $name = null, string $contentType = null)
    {
        $this->attachments[] = ['path' => $path, 'name' => $name, 'content-type' => $contentType, 'inline' => true];

        return $this;
    }

    /**
     * @return $this
     */
    public function attachPart(DataPart $part)
    {
        $this->attachments[] = ['part' => $part];

        return $this;
    }

    /**
     * @return array|DataPart[]
     */
    public function getAttachments(): array
    {
        $parts = [];
        foreach ($this->attachments as $attachment) {
            $parts[] = $this->createDataPart($attachment);
        }

        return $parts;
    }

    public function getBody(): AbstractPart
    {
        if (null !== $body = parent::getBody()) {
            return $body;
        }

        return $this->generateBody();
    }

    public function ensureValidity()
    {
        if (null === $this->text && null === $this->html && !$this->attachments) {
            throw new LogicException('A message must have a text or an HTML part or attachments.');
        }

        parent::ensureValidity();
    }

    /**
     * Generates an AbstractPart based on the raw body of a message.
     *
     * The most "complex" part generated by this method is when there is text and HTML bodies
     * with related images for the HTML part and some attachments:
     *
     * multipart/mixed
     *         |
     *         |------------> multipart/related
     *         |                      |
     *         |                      |------------> multipart/alternative
     *         |                      |                      |
     *         |                      |                       ------------> text/plain (with content)
     *         |                      |                      |
     *         |                      |                       ------------> text/html (with content)
     *         |                      |
     *         |                       ------------> image/png (with content)
     *         |
     *          ------------> application/pdf (with content)
     */
    private function generateBody(): AbstractPart
    {
        $this->ensureValidity();

        [$htmlPart, $attachmentParts, $inlineParts] = $this->prepareParts();

        $part = null === $this->text ? null : new TextPart($this->text, $this->textCharset);
        if (null !== $htmlPart) {
            if (null !== $part) {
                $part = new AlternativePart($part, $htmlPart);
            } else {
                $part = $htmlPart;
            }
        }

        if ($inlineParts) {
            $part = new RelatedPart($part, ...$inlineParts);
        }

        if ($attachmentParts) {
            if ($part) {
                $part = new MixedPart($part, ...$attachmentParts);
            } else {
                $part = new MixedPart(...$attachmentParts);
            }
        }

        return $part;
    }

    private function prepareParts(): ?array
    {
        $names = [];
        $htmlPart = null;
        $html = $this->html;
        if (null !== $this->html) {
            $htmlPart = new TextPart($html, $this->htmlCharset, 'html');
            $html = $htmlPart->getBody();
            preg_match_all('(<img\s+[^>]*src\s*=\s*(?:([\'"])cid:([^"]+)\\1|cid:([^>\s]+)))i', $html, $names);
            $names = array_filter(array_unique(array_merge($names[2], $names[3])));
        }

        $attachmentParts = $inlineParts = [];
        foreach ($this->attachments as $attachment) {
            foreach ($names as $name) {
                if (isset($attachment['part'])) {
                    continue;
                }
                if ($name !== $attachment['name']) {
                    continue;
                }
                if (isset($inlineParts[$name])) {
                    continue 2;
                }
                $attachment['inline'] = true;
                $inlineParts[$name] = $part = $this->createDataPart($attachment);
                $html = str_replace('cid:'.$name, 'cid:'.$part->getContentId(), $html);
                continue 2;
            }
            $attachmentParts[] = $this->createDataPart($attachment);
        }
        if (null !== $htmlPart) {
            $htmlPart = new TextPart($html, $this->htmlCharset, 'html');
        }

        return [$htmlPart, $attachmentParts, array_values($inlineParts)];
    }

    private function createDataPart(array $attachment): DataPart
    {
        if (isset($attachment['part'])) {
            return $attachment['part'];
        }

        if (isset($attachment['body'])) {
            $part = new DataPart($attachment['body'], $attachment['name'] ?? null, $attachment['content-type'] ?? null);
        } else {
            $part = DataPart::fromPath($attachment['path'] ?? '', $attachment['name'] ?? null, $attachment['content-type'] ?? null);
        }
        if ($attachment['inline']) {
            $part->asInline();
        }

        return $part;
    }

    /**
     * @return $this
     */
    private function setHeaderBody(string $type, string $name, $body): object
    {
        $this->getHeaders()->setHeaderBody($type, $name, $body);

        return $this;
    }

    private function addListAddressHeaderBody(string $name, array $addresses)
    {
        if (!$header = $this->getHeaders()->get($name)) {
            return $this->setListAddressHeaderBody($name, $addresses);
        }
        $header->addAddresses(Address::createArray($addresses));

        return $this;
    }

    private function setListAddressHeaderBody(string $name, array $addresses)
    {
        $addresses = Address::createArray($addresses);
        $headers = $this->getHeaders();
        if ($header = $headers->get($name)) {
            $header->setAddresses($addresses);
        } else {
            $headers->addMailboxListHeader($name, $addresses);
        }

        return $this;
    }

    /**
     * @internal
     */
    public function __serialize(): array
    {
        if (\is_resource($this->text)) {
            $this->text = (new TextPart($this->text))->getBody();
        }

        if (\is_resource($this->html)) {
            $this->html = (new TextPart($this->html))->getBody();
        }

        foreach ($this->attachments as $i => $attachment) {
            if (isset($attachment['body']) && \is_resource($attachment['body'])) {
                $this->attachments[$i]['body'] = (new TextPart($attachment['body']))->getBody();
            }
        }

        return [$this->text, $this->textCharset, $this->html, $this->htmlCharset, $this->attachments, parent::__serialize()];
    }

    /**
     * @internal
     */
    public function __unserialize(array $data): void
    {
        [$this->text, $this->textCharset, $this->html, $this->htmlCharset, $this->attachments, $parentData] = $data;

        parent::__unserialize($parentData);
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
object(MessageEvents)#1 (2) {
  ["events":"MessageEvents":private]=>
  array(2) {
    [0]=>
    object(MessageEvent)#2 (5) {
      ["message":"MessageEvent":private]=>
      object(Email)#3 (8) {
        ["text":"Email":private]=>
        NULL
        ["textCharset":"Email":private]=>
        NULL
        ["html":"Email":private]=>
        NULL
        ["htmlCharset":"Email":private]=>
        NULL
        ["attachments":"Email":private]=>
        array(0) {
        }
        ["headers":"Message":private]=>
        object(Headers)#4 (2) {
          ["headers":"Headers":private]=>
          array(1) {
            ["to"]=>
            array(1) {
              [0]=>
              object(MailboxListHeader)#6 (5) {
                ["addresses":"MailboxListHeader":private]=>
                array(1) {
                  [0]=>
                  object(Address)#5 (2) {
                    ["address":"Address":private]=>
                    string(17) "alice@example.com"
                    ["name":"Address":private]=>
                    string(0) ""
                  }
                }
                ["name":"AbstractHeader":private]=>
                string(2) "To"
                ["lineLength":"AbstractHeader":private]=>
                int(76)
                ["lang":"AbstractHeader":private]=>
                NULL
                ["charset":"AbstractHeader":private]=>
                string(5) "utf-8"
              }
            }
          }
          ["lineLength":"Headers":private]=>
          int(76)
        }
        ["body":"Message":private]=>
        NULL
        ["message":"RawMessage":private]=>
        NULL
      }
      ["envelope":"MessageEvent":private]=>
      object(DelayedEnvelope)#7 (5) {
        ["senderSet":"DelayedEnvelope":private]=>
        bool(false)
        ["recipientsSet":"DelayedEnvelope":private]=>
        bool(false)
        ["message":"DelayedEnvelope":private]=>
        object(Email)#3 (8) {
          ["text":"Email":private]=>
          NULL
          ["textCharset":"Email":private]=>
          NULL
          ["html":"Email":private]=>
          NULL
          ["htmlCharset":"Email":private]=>
          NULL
          ["attachments":"Email":private]=>
          array(0) {
          }
          ["headers":"Message":private]=>
          object(Headers)#4 (2) {
            ["headers":"Headers":private]=>
            array(1) {
              ["to"]=>
              array(1) {
                [0]=>
                object(MailboxListHeader)#6 (5) {
                  ["addresses":"MailboxListHeader":private]=>
                  array(1) {
                    [0]=>
                    object(Address)#5 (2) {
                      ["address":"Address":private]=>
                      string(17) "alice@example.com"
                      ["name":"Address":private]=>
                      string(0) ""
                    }
                  }
                  ["name":"AbstractHeader":private]=>
                  string(2) "To"
                  ["lineLength":"AbstractHeader":private]=>
                  int(76)
                  ["lang":"AbstractHeader":private]=>
                  NULL
                  ["charset":"AbstractHeader":private]=>
                  string(5) "utf-8"
                }
              }
            }
            ["lineLength":"Headers":private]=>
            int(76)
          }
          ["body":"Message":private]=>
          NULL
          ["message":"RawMessage":private]=>
          NULL
        }
        ["sender":"Envelope":private]=>
        NULL
        ["recipients":"Envelope":private]=>
        array(0) {
        }
      }
      ["transport":"MessageEvent":private]=>
      string(11) "null://null"
      ["queued":"MessageEvent":private]=>
      bool(false)
      ["propagationStopped":"Event":private]=>
      bool(false)
    }
    [1]=>
    object(MessageEvent)#8 (5) {
      ["message":"MessageEvent":private]=>
      object(Email)#9 (8) {
        ["text":"Email":private]=>
        NULL
        ["textCharset":"Email":private]=>
        NULL
        ["html":"Email":private]=>
        NULL
        ["htmlCharset":"Email":private]=>
        NULL
        ["attachments":"Email":private]=>
        array(0) {
        }
        ["headers":"Message":private]=>
        object(Headers)#10 (2) {
          ["headers":"Headers":private]=>
          array(1) {
            ["to"]=>
            array(1) {
              [0]=>
              object(MailboxListHeader)#12 (5) {
                ["addresses":"MailboxListHeader":private]=>
                array(1) {
                  [0]=>
                  object(Address)#11 (2) {
                    ["address":"Address":private]=>
                    string(15) "bob@example.com"
                    ["name":"Address":private]=>
                    string(0) ""
                  }
                }
                ["name":"AbstractHeader":private]=>
                string(2) "To"
                ["lineLength":"AbstractHeader":private]=>
                int(76)
                ["lang":"AbstractHeader":private]=>
                NULL
                ["charset":"AbstractHeader":private]=>
                string(5) "utf-8"
              }
            }
          }
          ["lineLength":"Headers":private]=>
          int(76)
        }
        ["body":"Message":private]=>
        NULL
        ["message":"RawMessage":private]=>
        NULL
      }
      ["envelope":"MessageEvent":private]=>
      object(DelayedEnvelope)#13 (5) {
        ["senderSet":"DelayedEnvelope":private]=>
        bool(false)
        ["recipientsSet":"DelayedEnvelope":private]=>
        bool(false)
        ["message":"DelayedEnvelope":private]=>
        object(Email)#9 (8) {
          ["text":"Email":private]=>
          NULL
          ["textCharset":"Email":private]=>
          NULL
          ["html":"Email":private]=>
          NULL
          ["htmlCharset":"Email":private]=>
          NULL
          ["attachments":"Email":private]=>
          array(0) {
          }
          ["headers":"Message":private]=>
          object(Headers)#10 (2) {
            ["headers":"Headers":private]=>
            array(1) {
              ["to"]=>
              array(1) {
                [0]=>
                object(MailboxListHeader)#12 (5) {
                  ["addresses":"MailboxListHeader":private]=>
                  array(1) {
                    [0]=>
                    object(Address)#11 (2) {
                      ["address":"Address":private]=>
                      string(15) "bob@example.com"
                      ["name":"Address":private]=>
                      string(0) ""
                    }
                  }
                  ["name":"AbstractHeader":private]=>
                  string(2) "To"
                  ["lineLength":"AbstractHeader":private]=>
                  int(76)
                  ["lang":"AbstractHeader":private]=>
                  NULL
                  ["charset":"AbstractHeader":private]=>
                  string(5) "utf-8"
                }
              }
            }
            ["lineLength":"Headers":private]=>
            int(76)
          }
          ["body":"Message":private]=>
          NULL
          ["message":"RawMessage":private]=>
          NULL
        }
        ["sender":"Envelope":private]=>
        NULL
        ["recipients":"Envelope":private]=>
        array(0) {
        }
      }
      ["transport":"MessageEvent":private]=>
      string(11) "null://null"
      ["queued":"MessageEvent":private]=>
      bool(false)
      ["propagationStopped":"Event":private]=>
      bool(false)
    }
  }
  ["transports":"MessageEvents":private]=>
  array(1) {
    ["null://null"]=>
    bool(true)
  }
}
string(14) "headers_before"
bool(false)
string(13) "headers_after"
bool(false)
