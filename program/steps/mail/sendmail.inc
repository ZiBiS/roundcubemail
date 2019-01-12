<?php

/**
 +-----------------------------------------------------------------------+
 | program/steps/mail/sendmail.inc                                       |
 |                                                                       |
 | This file is part of the Roundcube Webmail client                     |
 | Copyright (C) 2005-2017, The Roundcube Dev Team                       |
 |                                                                       |
 | Licensed under the GNU General Public License version 3 or            |
 | any later version with exceptions for skins & plugins.                |
 | See the README file for a full license statement.                     |
 |                                                                       |
 | PURPOSE:                                                              |
 |   Compose a new mail message and send it or store as draft            |
 +-----------------------------------------------------------------------+
 | Author: Thomas Bruederli <roundcube@gmail.com>                        |
 | Author: Aleksander Machniak <alec@alec.pl>                            |
 +-----------------------------------------------------------------------+
*/

// remove all scripts and act as called in frame
$OUTPUT->reset();
$OUTPUT->framed = true;

$COMPOSE_ID = rcube_utils::get_input_value('_id', rcube_utils::INPUT_GPC);
$COMPOSE    =& $_SESSION['compose_data_'.$COMPOSE_ID];

// Sanity checks
if (!isset($COMPOSE['id'])) {
    rcube::raise_error(array('code' => 500, 'type' => 'php',
        'file' => __FILE__, 'line' => __LINE__,
        'message' => "Invalid compose ID"), true, false);

    $OUTPUT->show_message('internalerror', 'error');
    $OUTPUT->send('iframe');
}

$saveonly  = !empty($_GET['_saveonly']);
$savedraft = !empty($_POST['_draft']) && !$saveonly;
$SENDMAIL  = new rcmail_sendmail($COMPOSE, array(
        'sendmail'      => true,
        'saveonly'      => $saveonly,
        'savedraft'     => $savedraft,
        'error_handler' => function() use ($OUTPUT) {
            call_user_func_array(array($OUTPUT, 'show_message'), func_get_args());
            $OUTPUT->send('iframe');
        }
));

// Collect input for message headers
$headers = $SENDMAIL->headers_input();

$COMPOSE['param']['message-id'] = $headers['Message-ID'];

$message_id      = $headers['Message-ID'];
$message_charset = $SENDMAIL->options['charset'];
$message_body    = rcube_utils::get_input_value('_message', rcube_utils::INPUT_POST, true, $message_charset);
$isHtml          = (bool) rcube_utils::get_input_value('_is_html', rcube_utils::INPUT_POST);

// Reset message body and attachments in Mailvelope mode
if (isset($_POST['_pgpmime'])) {
    $pgp_mime     = rcube_utils::get_input_value('_pgpmime', rcube_utils::INPUT_POST);
    $isHtml       = false;
    $message_body = '';

    // clear unencrypted attachments
    foreach ((array) $COMPOSE['attachments'] as $attach) {
        $RCMAIL->plugins->exec_hook('attachment_delete', $attach);
    }

    $COMPOSE['attachments'] = array();
}

if ($isHtml) {
    $bstyle = array();

    if ($font_size = $RCMAIL->config->get('default_font_size')) {
        $bstyle[] = 'font-size: ' . $font_size;
    }
    if ($font_family = $RCMAIL->config->get('default_font')) {
        $bstyle[] = 'font-family: ' . rcmail::font_defs($font_family);
    }

    // append doctype and html/body wrappers
    $bstyle       = !empty($bstyle) ? (" style='" . implode($bstyle, '; ') . "'") : '';
    $message_body = '<html><head>'
        . '<meta http-equiv="Content-Type" content="text/html; charset='
        . ($message_charset ?: RCUBE_CHARSET) . '" /></head>'
        . "<body" . $bstyle . ">\r\n" . $message_body;
}

if (!$savedraft) {
    if ($isHtml) {
        $b_style   = 'padding: 0 0.4em; border-left: #1010ff 2px solid; margin: 0';
        $pre_style = 'margin: 0; padding: 0; font-family: monospace';

        $message_body = preg_replace(
            array(
                // remove empty signature div
                '/<div id="_rc_sig">(&nbsp;)?<\/div>[\s\r\n]*$/',
                // replace signature's div ID (#6073)
                '/ id="_rc_sig"/',
                // add inline css for blockquotes and container
                '/<blockquote>/',
                '/<div class="pre">/',
                // convert TinyMCE's new-line sequences (#1490463)
                '/<p>&nbsp;<\/p>/',
            ),
            array(
                '',
                ' id="signature"',
                '<blockquote type="cite" style="'.$b_style.'">',
                '<div class="pre" style="'.$pre_style.'">',
                '<p><br /></p>',
            ),
            $message_body);

        rcube_utils::preg_error(array(
                'line'    => __LINE__,
                'file'    => __FILE__,
                'message' => "Could not format HTML!"
            ), true);
    }

    // Check spelling before send
    if ($RCMAIL->config->get('spellcheck_before_send')
        && $RCMAIL->config->get('enable_spellcheck')
        && empty($COMPOSE['spell_checked'])
        && !empty($message_body)
    ) {
        $message_body = str_replace("\r\n", "\n", $message_body);
        $spellchecker = new rcube_spellchecker(rcube_utils::get_input_value('_lang', rcube_utils::INPUT_GPC));
        $spell_result = $spellchecker->check($message_body, $isHtml);

        $COMPOSE['spell_checked'] = true;

        if (!$spell_result) {
            if ($isHtml) {
                $result['words']      = $spellchecker->get();
                $result['dictionary'] = (bool) $RCMAIL->config->get('spellcheck_dictionary');
            }
            else {
                $result = $spellchecker->get_xml();
            }

            $OUTPUT->show_message('mispellingsfound', 'error');
            $OUTPUT->command('spellcheck_resume', $result);
            $OUTPUT->send('iframe');
        }
    }

    // generic footer for all messages
    if ($footer = $SENDMAIL->generic_message_footer($isHtml)) {
        $message_body .= "\r\n" . $footer;
    }
}

if ($isHtml) {
    $message_body .= "\r\n</body></html>\r\n";
}

// sort attachments to make sure the order is the same as in the UI (#1488423)
if ($files = rcube_utils::get_input_value('_attachments', rcube_utils::INPUT_POST)) {
    $files = explode(',', $files);
    $files = array_flip($files);
    foreach ($files as $idx => $val) {
        $files[$idx] = $COMPOSE['attachments'][$idx];
        unset($COMPOSE['attachments'][$idx]);
    }

    $COMPOSE['attachments'] = array_merge(array_filter($files), $COMPOSE['attachments']);
}

// Since we can handle big messages with disk usage, we need more time to work
@set_time_limit(360);

// create PEAR::Mail_mime instance, set headers, body and params
$MAIL_MIME = $SENDMAIL->create_message($headers, $message_body, $isHtml, $COMPOSE['attachments']);

// add stored attachments, if any
if (is_array($COMPOSE['attachments'])) {
    foreach ($COMPOSE['attachments'] as $id => $attachment) {
        // This hook retrieves the attachment contents from the file storage backend
        $attachment = $RCMAIL->plugins->exec_hook('attachment_get', $attachment);
        $is_inline  = false;

        if ($isHtml) {
            $dispurl      = '/[\'"]\S+display-attachment\S+file=rcmfile' . preg_quote($attachment['id']) . '[\'"]/';
            $message_body = $MAIL_MIME->getHTMLBody();
            $is_inline    = preg_match($dispurl, $message_body);
        }

        // inline image
        if ($is_inline) {
            // Mail_Mime does not support many inline attachments with the same name (#1489406)
            // we'll generate cid: urls here to workaround this
            $cid = preg_replace('/[^0-9a-zA-Z]/', '', uniqid(time(), true));
            if (preg_match('#(@[0-9a-zA-Z\-\.]+)#', $SENDMAIL->options['from'], $matches)) {
                $cid .= $matches[1];
            }
            else {
                $cid .= '@localhost';
            }

            $message_body = preg_replace($dispurl, '"cid:' . $cid . '"', $message_body);

            rcube_utils::preg_error(array(
                    'line'    => __LINE__,
                    'file'    => __FILE__,
                    'message' => "Could not replace an image reference!"
                ), true);

            $MAIL_MIME->setHTMLBody($message_body);

            if ($attachment['data'])
                $MAIL_MIME->addHTMLImage($attachment['data'], $attachment['mimetype'], $attachment['name'], false, $cid);
            else
                $MAIL_MIME->addHTMLImage($attachment['path'], $attachment['mimetype'], $attachment['name'], true, $cid);
        }
        else {
            $ctype   = str_replace('image/pjpeg', 'image/jpeg', $attachment['mimetype']); // #1484914
            $file    = $attachment['data'] ?: $attachment['path'];
            $folding = (int) $RCMAIL->config->get('mime_param_folding');

            $MAIL_MIME->addAttachment($file,
                $ctype,
                $attachment['name'],
                $attachment['data'] ? false : true,
                $ctype == 'message/rfc822' ? '8bit' : 'base64',
                'attachment',
                $attachment['charset'],
                '', '',
                $folding ? 'quoted-printable' : NULL,
                $folding == 2 ? 'quoted-printable' : NULL,
                '', RCUBE_CHARSET
            );
        }
    }
}

// compose PGP/Mime message
if ($pgp_mime) {
    $MAIL_MIME->addAttachment(new Mail_mimePart('Version: 1', array(
            'content_type' => 'application/pgp-encrypted',
            'description'  => 'PGP/MIME version identification',
    )));

    $MAIL_MIME->addAttachment(new Mail_mimePart($pgp_mime, array(
            'content_type' => 'application/octet-stream',
            'filename'     => 'encrypted.asc',
            'disposition'  => 'inline',
    )));

    $MAIL_MIME->setContentType('multipart/encrypted', array('protocol' => 'application/pgp-encrypted'));
    $MAIL_MIME->setParam('preamble', 'This is an OpenPGP/MIME encrypted message (RFC 2440 and 3156)');
}

// This hook allows to modify the message before send or save action
$plugin    = $RCMAIL->plugins->exec_hook('message_ready', array('message' => $MAIL_MIME));
$MAIL_MIME = $plugin['message'];

// Deliver the message over SMTP
if (!$savedraft && !$saveonly) {
    $sent = $SENDMAIL->deliver_message($MAIL_MIME);
}

// Save the message in Drafts/Sent
$saved = $SENDMAIL->save_message($MAIL_MIME);

// raise error if saving failed
if (!$saved && $savedraft) {
    $RCMAIL->display_server_error('errorsaving');
    // start the auto-save timer again
    $OUTPUT->command('auto_save_start');
    $OUTPUT->send('iframe');
}

$store_target = $SENDMAIL->options['store_target'];
$store_folder = $SENDMAIL->options['store_folder'];

// delete previous saved draft
$drafts_mbox = $RCMAIL->config->get('drafts_mbox');
$old_id      = rcube_utils::get_input_value('_draft_saveid', rcube_utils::INPUT_POST);

if ($old_id && ($sent || $saved)) {
    $deleted = $RCMAIL->storage->delete_message($old_id, $drafts_mbox);

    // raise error if deletion of old draft failed
    if (!$deleted) {
        rcube::raise_error(array('code' => 800, 'type' => 'imap',
            'file' => __FILE__, 'line' => __LINE__,
            'message' => "Could not delete message from $drafts_mbox"), true, false);
    }
}

if ($savedraft) {
    // remember new draft-uid ($saved could be an UID or true/false here)
    if ($saved && is_bool($saved)) {
        $index = $RCMAIL->storage->search_once($drafts_mbox, 'HEADER Message-ID ' . $message_id);
        $saved = @max($index->get());
    }

    if ($saved) {
        $plugin = $RCMAIL->plugins->exec_hook('message_draftsaved',
            array('msgid' => $message_id, 'uid' => $saved, 'folder' => $store_target));

        // display success
        $OUTPUT->show_message($plugin['message'] ?: 'messagesaved', 'confirmation');

        // update "_draft_saveid" and the "cmp_hash" to prevent "Unsaved changes" warning
        $COMPOSE['param']['draft_uid'] = $plugin['uid'];
        $OUTPUT->command('set_draft_id', $plugin['uid']);
        $OUTPUT->command('compose_field_hash', true);
    }

    // start the auto-save timer again
    $OUTPUT->command('auto_save_start');
}
else {
    // Collect folders which could contain the composed message,
    // we'll refresh the list if currently opened folder is one of them (#1490238)
    $folders = array();

    if (!$saveonly) {
        if (in_array($COMPOSE['mode'], array('reply', 'forward', 'draft'))) {
            $folders[] = $COMPOSE['mailbox'];
        }
        if (!empty($COMPOSE['param']['draft_uid']) && $drafts_mbox) {
            $folders[] = $drafts_mbox;
        }
    }

    if ($store_folder && !$saved) {
        $params = $saveonly ? null : array('prefix' => true);
        $RCMAIL->display_server_error('errorsavingsent', null, null, $params);
        if ($saveonly) {
            $OUTPUT->send('iframe');
        }

        $save_error = true;
    }
    else {
        $RCMAIL->plugins->exec_hook('attachments_cleanup', array('group' => $COMPOSE_ID));
        $RCMAIL->session->remove('compose_data_' . $COMPOSE_ID);
        $_SESSION['last_compose_session'] = $COMPOSE_ID;

        $OUTPUT->command('remove_compose_data', $COMPOSE_ID);

        if ($store_folder) {
            $folders[] = $store_target;
        }
    }

    $msg = $RCMAIL->gettext($saveonly ? 'successfullysaved' : 'messagesent');

    $OUTPUT->command('sent_successfully', 'confirmation', $msg, $folders, $save_error);
}

$OUTPUT->send('iframe');


/****** message sending functions ********/

function rcmail_received_host($host)
{
    $hostname = gethostbyaddr($host);

    $result = rcmail_encrypt_host($hostname);

    if ($host != $hostname) {
        $result .= ' (' . rcmail_encrypt_host($host) . ')';
    }

    return $result;
}

// encrypt host IP or hostname for Received header
function rcmail_encrypt_host($host)
{
    global $RCMAIL;

    if ($RCMAIL->config->get('http_received_header_encrypt')) {
        return $RCMAIL->encrypt($host);
    }

    if (!preg_match('/[^0-9:.]/', $host)) {
        return "[$host]";
    }

    return $host;
}

// get identity record
function rcmail_get_identity($id)
{
    global $RCMAIL, $message_charset;

    if ($sql_arr = $RCMAIL->user->get_identity($id)) {
        $out = $sql_arr;

        if ($message_charset != RCUBE_CHARSET) {
            foreach ($out as $k => $v) {
                $out[$k] = rcube_charset::convert($v, RCUBE_CHARSET, $message_charset);
            }
        }

        $out['mailto'] = $sql_arr['email'];
        $out['string'] = format_email_recipient($sql_arr['email'], $sql_arr['name']);

        return $out;
    }

    return false;
}

/**
 * Extract image attachments from HTML content (data URIs)
 */
function rcmail_extract_inline_images($mime_message, $from)
{
    $body   = $mime_message->getHTMLBody();
    $offset = 0;
    $list   = array();
    $domain = 'localhost';
    $regexp = '#img[^>]+src=[\'"](data:([^;]*);base64,([a-z0-9+/=\r\n]+))([\'"])#i';

    if (preg_match_all($regexp, $body, $matches, PREG_OFFSET_CAPTURE)) {
        // get domain for the Content-ID, must be the same as in Mail_Mime::get()
        if (preg_match('#@([0-9a-zA-Z\-\.]+)#', $from, $m)) {
            $domain = $m[1];
        }

        foreach ($matches[1] as $idx => $m) {
            $data = preg_replace('/\r\n/', '', $matches[3][$idx][0]);
            $data = base64_decode($data);

            if (empty($data)) {
                continue;
            }

            $hash      = md5($data) . '@' . $domain;
            $mime_type = $matches[2][$idx][0];
            $name      = $list[$hash];

            if (empty($mime_type)) {
                $mime_type = rcube_mime::image_content_type($data);
            }

            // add the image to the MIME message
            if (!$name) {
                $ext         = preg_replace('#^[^/]+/#', '', $mime_type);
                $name        = substr($hash, 0, 8) . '.' . $ext;
                $list[$hash] = $name;

                $mime_message->addHTMLImage($data, $mime_type, $name, false, $hash);
            }

            $body = substr_replace($body, $name, $m[1] + $offset, strlen($m[0]));
            $offset += strlen($name) - strlen($m[0]);
        }
    }

    $mime_message->setHTMLBody($body);
}

/**
 * Parse and cleanup email address input (and count addresses)
 *
 * @param string  Address input
 * @param boolean Do count recipients (saved in global $RECIPIENT_COUNT)
 * @param boolean Validate addresses (errors saved in global $EMAIL_FORMAT_ERROR)
 * @return string Canonical recipients string separated by comma
 */
function rcmail_email_input_format($mailto, $count=false, $check=true)
{
    global $RCMAIL, $EMAIL_FORMAT_ERROR, $RECIPIENT_COUNT;

    // simplified email regexp, supporting quoted local part
    $email_regexp = '(\S+|("[^"]+"))@\S+';

    $delim   = trim($RCMAIL->config->get('recipients_separator', ','));
    $regexp  = array("/[,;$delim]\s*[\r\n]+/", '/[\r\n]+/', "/[,;$delim]\s*\$/m", '/;/', '/(\S{1})(<'.$email_regexp.'>)/U');
    $replace = array($delim.' ', ', ', '', $delim, '\\1 \\2');

    // replace new lines and strip ending ', ', make address input more valid
    $mailto = trim(preg_replace($regexp, $replace, $mailto));
    $items  = rcube_utils::explode_quoted_string($delim, $mailto);
    $result = array();

    foreach ($items as $item) {
        $item = trim($item);
        // address in brackets without name (do nothing)
        if (preg_match('/^<'.$email_regexp.'>$/', $item)) {
            $item     = rcube_utils::idn_to_ascii(trim($item, '<>'));
            $result[] = $item;
        }
        // address without brackets and without name (add brackets)
        else if (preg_match('/^'.$email_regexp.'$/', $item)) {
            $item     = rcube_utils::idn_to_ascii($item);
            $result[] = $item;
        }
        // address with name (handle name)
        else if (preg_match('/<*'.$email_regexp.'>*$/', $item, $matches)) {
            $address = $matches[0];
            $name    = trim(str_replace($address, '', $item));
            if ($name[0] == '"' && $name[strlen($name)-1] == '"') {
                $name = substr($name, 1, -1);
            }
            $name     = stripcslashes($name);
            $address  = rcube_utils::idn_to_ascii(trim($address, '<>'));
            $result[] = format_email_recipient($address, $name);
            $item     = $address;
        }

        // check address format
        $item = trim($item, '<>');
        if ($item && $check && !rcube_utils::check_email($item)) {
            $EMAIL_FORMAT_ERROR = $item;
            return;
        }
    }

    if ($count) {
        $RECIPIENT_COUNT += count($result);
    }

    return implode(', ', $result);
}


function rcmail_generic_message_footer($isHtml)
{
    global $RCMAIL;

    if ($isHtml && ($file = $RCMAIL->config->get('generic_message_footer_html'))) {
        $html_footer = true;
    }
    else {
        $file = $RCMAIL->config->get('generic_message_footer');
        $html_footer = false;
    }

    if ($file && realpath($file)) {
        // sanity check
        if (!preg_match('/\.(php|ini|conf)$/', $file) && strpos($file, '/etc/') === false) {
            $footer = file_get_contents($file);
            if ($isHtml && !$html_footer) {
                $t2h    = new rcube_text2html($footer, false);
                $footer = $t2h->get_html();
            }
            return $footer;
        }
    }

    return false;
}

/**
 * clear message composing settings
 */
function rcmail_compose_cleanup($id)
{
    if (!isset($_SESSION['compose_data_'.$id])) {
        return;
    }

    $rcmail = rcmail::get_instance();
    $rcmail->plugins->exec_hook('attachments_cleanup', array('group' => $id));
    $rcmail->session->remove('compose_data_'.$id);

    $_SESSION['last_compose_session'] = $id;
}
