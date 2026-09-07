<?php

/**
 * Password Complexity Plugin
 *
 * Adds real-time password complexity validation to the password change form.
 * Works alongside the password plugin without modifying it.
 *
 * @author Zbigniew Szmyd
 * @license GPL-3.0+
 */
class password_complexity extends rcube_plugin
{
    public $task = 'settings';

    private $rules = [];

    function init()
    {
        $this->load_config();
        $this->add_texts('localization/', false);
        $this->add_hook('render_page', [$this, 'render_page']);
    }

    function render_page($p)
    {
        $rcmail = rcmail::get_instance();

        if ($rcmail->action !== 'plugin.password' && $rcmail->action !== 'plugin.password-save') {
            return $p;
        }

        $this->rules = $this->_build_rules();

        $rcmail->output->set_env('password_complexity_rules', $this->rules);
        $rcmail->output->add_label(
            'password_complexity.complexity_not_met'
        );
        $this->include_script('password_complexity.js');
        $this->include_stylesheet('password_complexity.css');

        return $p;
    }

    private function _build_rules()
    {
        $config_rules = rcmail::get_instance()->config->get('password_complexity_rules', []);
        $rules = [];

        foreach ($config_rules as $rule) {
            $type  = $rule['type'] ?? '';
            $value = $rule['value'] ?? 0;

            if (!$type || $value < 1) {
                continue;
            }

            $label_key = $rule['label_key'] ?? 'rule_' . $type;
            $label = $this->gettext(['name' => $label_key, 'vars' => ['count' => $value]]);

            $rules[] = [
                'type'  => $type,
                'value' => (int) $value,
                'label' => $label,
            ];
        }

        return $rules;
    }
}
