/**
 * Password Complexity - real-time password validation
 *
 * @author Zbigniew Szmyd
 * @license GPL-3.0+
 */
window.rcmail && rcmail.addEventListener('init', function() {
    var form = document.getElementById('password-form');
    if (!form) return;

    var rules = rcmail.env.password_complexity_rules || [];
    if (!rules.length) return;

    var newpasswd = document.getElementById('newpasswd');
    if (!newpasswd) return;

    var container = _buildRulesUI(rules);
    var ruleslist = document.getElementById('ruleslist');

    if (ruleslist) {
        ruleslist.parentNode.insertBefore(container, ruleslist.nextSibling);
    } else {
        var formContent = form.querySelector('.propform');
        if (formContent) {
            formContent.parentNode.insertBefore(container, formContent.nextSibling);
        }
    }

    newpasswd.addEventListener('input', function() {
        _validateRules(this.value, rules, container);
    });

    rcmail.addEventListener('beforeplugin.password-save', function() {
        if (!_allRulesPassed(newpasswd.value, rules)) {
            rcmail.display_message(rcmail.get_label('complexity_not_met', 'password_complexity'), 'error');
            newpasswd.focus();
            return false;
        }
    });

    _validateRules('', rules, container);

    function _buildRulesUI(rules) {
        var ul = document.createElement('ul');
        ul.id = 'complexity-rules';
        ul.className = 'hint proplist';

        for (var i = 0; i < rules.length; i++) {
            var li = document.createElement('li');
            li.className = 'complexity-rule d-block rule-pending';
            li.setAttribute('data-rule-index', i);

            var icon = document.createElement('span');
            icon.className = 'rule-icon';
            icon.textContent = '✗';

            var text = document.createElement('span');
            text.className = 'rule-text';
            text.textContent = rules[i].label;

            li.appendChild(icon);
            li.appendChild(text);
            ul.appendChild(li);
        }

        return ul;
    }

    function _checkRule(value, rule) {
        switch (rule.type) {
            case 'min_length':
                return value.length >= rule.value;
            case 'uppercase':
                return (value.match(/[\p{Lu}]/gu) || []).length >= rule.value;
            case 'lowercase':
                return (value.match(/[\p{Ll}]/gu) || []).length >= rule.value;
            case 'digit':
                return (value.match(/[0-9]/g) || []).length >= rule.value;
            case 'special':
                return (value.match(/[^\p{L}\p{N}]/gu) || []).length >= rule.value;
            default:
                return true;
        }
    }

    function _validateRules(value, rules, container) {
        var items = container.querySelectorAll('.complexity-rule');

        for (var i = 0; i < rules.length; i++) {
            var passed = value.length > 0 && _checkRule(value, rules[i]);
            var li = items[i];

            if (!li) continue;

            var icon = li.querySelector('.rule-icon');

            if (value.length === 0) {
                li.className = 'complexity-rule d-block rule-pending';
                icon.textContent = '✗';
            } else if (passed) {
                li.className = 'complexity-rule d-block rule-pass';
                icon.textContent = '✓';
            } else {
                li.className = 'complexity-rule d-block rule-fail';
                icon.textContent = '✗';
            }
        }
    }

    function _allRulesPassed(value, rules) {
        for (var i = 0; i < rules.length; i++) {
            if (!_checkRule(value, rules[i])) return false;
        }
        return true;
    }
});
