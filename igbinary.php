<?php
/**
 * Instead of the time and space consuming textual representation used by PHP's `serialize`,
 * igbinary stores php data structures in a compact binary form.
 * Memory savings are significant when using memcached, APCu, or similar memory based storages for serialized data.
 * The typical reduction in storage requirements are around 50%.
 * The exact percentage depends on your data.
 *
 * But where does the name "igbinary" come from? There was once a similar project
 * called fbinary but it has disappeared from the Internet. Its architecture
 * wasn't very clean either. IG is short name for a finnish social networking site
 * {@link http://irc-galleria.net/ IRC-Galleria}.
 *
 * Storing complex PHP data structures such as arrays of associative arrays
 * with the standard PHP serializer is not very space efficient.
 * Igbinary uses two strategies to minimize the size of the serialized
 * output.
 *
 * 1. Repeated strings are stored only once (this also includes class and property names).
 *    Collections of objects benefit significantly from this.
 *    See the `igbinary.compact_strings` option.
 *
 * 2. Integer values are stored in the smallest primitive data type available:
 *     *123* = `int8_t`,
 *     *1234* = `int16_t`,
 *     *123456* = `int32_t`
 *    ... and so on.
 *
 * This file is igbinary's phpdoc documentation stub.
 *
 * @author Oleg Grenrus <oleg.grenrus@dynamoid.com>
 * @version 1.0.0
 * @package igbinary
 */

/**
 * Generates a storable representation of a value.
 * This is useful for storing or passing PHP values around without losing their type and structure.
 * To make the serialized string into a PHP value again, use {@link igbinary_unserialize}.
 *
 * igbinary_serialize() handles all types, except the resource-type.
 * You can even serialize() arrays that contain references to itself.
 * Circular references inside the array/object you are serialize()ing will also be stored.
 *
 * If object implements {@link http://www.php.net/~helly/php/ext/spl/interfaceSerializable.html Serializable} -interface,
 * PHP will call the member function serialize to get serialized representation of object.
 *
 * When serializing objects, PHP will attempt to call the member function __sleep prior to serialization.
 * This is to allow the object to do any last minute clean-up, etc. prior to being serialized.
 * Likewise, when the object is restored using unserialize() the __wakeup member function is called.
 *
 * @param mixed $value The value to be serialized.
 * @return string Returns a string containing a binary representation of value that can be stored anywhere.
 * @link http://www.php.net/serialize PHP's default serialize
 */
function igbinary_serialize($value);

/** Creates a PHP value from a stored representation.
 * igbinary_unserialize() takes a single serialized variable and converts it back into a PHP value.
 *
 * If the variable being unserialized is an object,
 * then after successfully reconstructing the object,
 * PHP will automatically call the __wakeup() member function (if it exists).
 *
 * If the passed in string could not be unserialized,
 * then NULL is returned and an E_WARNING is issued.
 *
 * @param string $str The serialized string.
 * @param array $options The same options as unserialize():
 *   - "allowed_classes": an array of the names of the classes to instantiate, false to instantiate none, or true to instantiate all (the default).
 *     The other classes are unserialized as __PHP_Incomplete_Class objects. Enums are not affected.
 *   - "max_depth": the maximum depth of nested arrays and objects, or 0 to disable the limit. Defaults to the unserialize_max_depth ini setting.
 * @return mixed The unserialized value is returned. It can be a boolean, integer, float, string, array, object or null.
 * @link http://www.php.net/manual/en/function.unserialize.php PHP's default unserialize
 * @link https://secure.php.net/serializable Serializable interface
 */
function igbinary_unserialize($str, array $options = []);
