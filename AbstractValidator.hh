<?hh // strict
/**
 * @copyright   2010-2015, The Titon Project
 * @license     http://opensource.org/licenses/bsd-license.php
 * @link        http://titon.io
 */

namespace Titon\Validate;

use Titon\Validate\Exception\MissingConstraintException;
use Titon\Validate\Exception\MissingMessageException;
use Titon\Utility\Str;
use \Indexish;
use \ReflectionClass;
use \InvalidArgumentException;

/**
 * Defines shared functionality for validators.
 *
 * @package Titon\Validate
 */
abstract class AbstractValidator implements Validator {

    /**
     * Constraint callbacks mapped by rule name.
     *
     * @var \Titon\Validate\ConstraintMap
     */
    protected ConstraintMap $constraints = Map {};

    /**
     * Data to validate against.
     *
     * @var \Titon\Validate\DataMap
     */
    protected DataMap $data = Map {};

    /**
     * Errors gathered during validation.
     *
     * @var \Titon\Validate\ErrorMap
     */
    protected ErrorMap $errors = Map {};

    /**
     * Mapping of fields and titles.
     *
     * @var \Titon\Validate\FieldMap
     */
    protected FieldMap $fields = Map {};

    /**
     * Fallback mapping of error messages.
     *
     * @var \Titon\Validate\MessageMap
     */
    protected MessageMap $messages = Map {};

    /**
     * Mapping of fields and validation rules.
     *
     * @var \Titon\Validate\RuleContainer
     */
    protected RuleContainer $rules = Map {};

    /**
     * Store the data to validate.
     *
     * @param \Titon\Validate\DataMap $data
     */
    public function __construct(DataMap $data = Map {}) {
        $this->setData($data);
    }

    /**
     * {@inheritdoc}
     */
    public function addConstraint(string $key, ConstraintCallback $callback): this {
        $this->constraints[$key] = $callback;

        return $this;
    }

    /**
     * {@inheritdoc}
     */
    public function addConstraintProvider(ConstraintProvider $provider): this {
        $this->constraints->setAll($provider->getConstraints());

        return $this;
    }

    /**
     * {@inheritdoc}
     */
    public function addError(string $field, string $message): this {
        $this->errors[$field] = $message;

        return $this;
    }

    /**
     * {@inheritdoc}
     */
    public function addField(string $field, string $title, Map<string, OptionList> $rules = Map {}): this {
        $this->fields[$field] = $title;

        if (!$rules->isEmpty()) {
            foreach ($rules as $rule => $options) {
                $this->addRule($field, $rule, '', $options);
            }
        }

        return $this;
    }

    /**
     * {@inheritdoc}
     */
    public function addMessages(MessageMap $messages): this {
        $this->messages->setAll($messages);

        return $this;
    }

    /**
     * {@inheritdoc}
     *
     * @throws \InvalidArgumentException
     */
    public function addRule(string $field, string $rule, string $message, OptionList $options = Vector{}): this {
        if (!$this->fields->contains($field)) {
            throw new InvalidArgumentException(sprintf('Field %s does not exist', $field));
        }

        if ($this->messages->contains($rule)) {
            $message = $message ?: $this->messages[$rule];
        } else {
            $this->messages[$rule] = $message;
        }

        if (!$this->rules->contains($field)) {
            $this->rules[$field] = Map {};
        }

        $this->rules[$field][$rule] = shape(
            'rule' => $rule,
            'message' => $message,
            'options' => $options
        );

        return $this;
    }

    /**
     * Format an error message by inserting tokens for the current field, rule, and rule options.
     *
     * @param string $field
     * @param \Titon\Validate\Rule $rule
     * @return string
     * @throws \Titon\Validate\Exception\MissingMessageException
     */
    public function formatMessage(string $field, Rule $rule): string {
        $message = $rule['message'] ?: $this->getMessages()->get($rule['rule']);

        if (!$message) {
            throw new MissingMessageException(sprintf('Error message for rule %s does not exist', $rule['rule']));
        }

        $tokens = Map {
            'field' => $field,
            'title' => $this->getFields()->get($field)
        };

        foreach ($rule['options'] as $i => $option) {
            $tokens[(string) $i] = ($option instanceof Indexish) ? implode(', ', $option) : $option;
        }

        return Str::insert($message, $tokens);
    }

    /**
     * {@inheritdoc}
     */
    public function getConstraints(): ConstraintMap {
        return $this->constraints;
    }

    /**
     * {@inheritdoc}
     */
    public function getData(): DataMap {
        return $this->data;
    }

    /**
     * {@inheritdoc}
     */
    public function getErrors(): ErrorMap {
        return $this->errors;
    }

    /**
     * {@inheritdoc}
     */
    public function getFields(): FieldMap {
        return $this->fields;
    }

    /**
     * {@inheritdoc}
     */
    public function getMessages(): MessageMap {
        return $this->messages;
    }

    /**
     * {@inheritdoc}
     */
    public function getRules(): RuleContainer {
        return $this->rules;
    }

    /**
     * {@inheritdoc}
     */
    public function reset(): this {
        $this->data->clear();
        $this->errors->clear();

        return $this;
    }

    /**
     * {@inheritdoc}
     */
    public function setData(DataMap $data): this {
        $this->data = $data;

        return $this;
    }

    /**
     * {@inheritdoc}
     *
     * @throws \Titon\Validate\Exception\MissingConstraintException
     */
    public function validate(DataMap $data = Map {}): bool {
        if ($data) {
            $this->setData($data);
        } else if (!$this->data) {
            return false;
        }

        $fieldRules = $this->getRules();
        $constraints = $this->getConstraints();

        foreach ($this->getData() as $field => $value) {
            if (!$fieldRules->contains($field)) {
                continue;
            }

            $rules = $fieldRules[$field];

            foreach ($rules as $rule => $params) {
                if (!$constraints->contains($rule)) {
                    throw new MissingConstraintException(sprintf('Validation constraint %s does not exist', $rule));
                }

                $arguments = $params['options']->toArray();

                // Add the input to validate as the 1st argument
                array_unshift($arguments, $value);

                // Execute the constraint
                if (!call_user_func_array($constraints[$rule], $arguments)) {
                    $this->addError($field, $this->formatMessage($field, $params));
                }
            }
        }

        return (count($this->errors) === 0);
    }

    /**
     * Create a validator instance from a set of shorthand or expanded rule sets.
     *
     * @param \Titon\Validate\DataMap $data
     * @param Map<string, mixed> $fields
     * @return $this
     */
    public static function makeFromShorthand(DataMap $data = Map {}, Map<string, mixed> $fields = Map {}): Validator {
        $class = new ReflectionClass(static::class);

        /** @var \Titon\Validate\Validator $obj */
        $obj = $class->newInstanceArgs([$data]);

        foreach ($fields as $field => $options) {
            $title = $field;

            // A string of rule(s)
            if (is_string($options)) {
                $options = Map {'rules' => $options};

            // List of rules
            } else if ($options instanceof Vector) {
                $options = Map {'rules' => $options};

            // Ignore anything else not a map
            } else if (!$options instanceof Map) {
                continue;
            }

            // Prepare for parsing
            if ($options->contains('title')) {
                $title = $options['title'];
            }

            if (is_string($options['rules'])) {
                $options['rules'] = new Vector(explode('|', $options['rules']));
            }

            $obj->addField($field, (string) $title);

            // Dereference for the type checker
            $rules = $options['rules'];

            if ($rules instanceof Vector) {
                foreach ($rules as $ruleOpts) {
                    $shorthand = static::splitShorthand($ruleOpts);

                    $obj->addRule($field, $shorthand['rule'], $shorthand['message'], $shorthand['options']);
                }
            }
        }

        return $obj;
    }

    /**
     * Split a shorthand rule into multiple parts.
     *
     * @param string $shorthand
     * @return \Titon\Validate\Rule
     */
    public static function splitShorthand(string $shorthand): Rule {
        $rule = '';
        $message = '';
        $opts = Vector {};

        // rule:o1,o2,o3
        // rule:o1,o2:The message here!
        if (strpos($shorthand, ':') !== false) {
            foreach (explode(':', $shorthand, 3) as $index => $part) {
                if ($index == 0) {
                    $rule = $part;

                } else if ($index == 1) {
                    if (strpos($part, ',') !== false) {
                        $opts = new Vector(explode(',', $part));
                    } else if ($part) {
                        $opts = new Vector([$part]);
                    }

                } else if ($index == 2) {
                    $message = $part;
                }
            }

        // rule
        } else {
            $rule = $shorthand;
        }

        return shape(
            'rule' => $rule,
            'message' => $message,
            'options' => $opts
        );
    }

}
