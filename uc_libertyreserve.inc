<?php

/**
 * @file
 * Callbacks and a hash function for the uc_libertyreserve module.
 */
function uc_libertyreserve_payment_completed() {
  $order_id = intval($_SESSION['cart_order']);
  $order = uc_order_load($order_id);
  $amount = $_POST['lr_amnt'];
  $description = t('A payment from Onpay was accepted.');

  uc_payment_enter($order_id, 'uc_libertyreserve', $amount, $order->uid, null, $description);
  uc_order_comment_save($order_id, 0, $description, 'admin');
  uc_cart_complete_sale($order);

  sleep(1);

  $q = db_select('uc_libertyreserve_notification');
  $q->addExpression('COUNT(*)', 'count');
  $q->condition('order_id', $order_id);
  $q->condition('lr_encrypted2_expected', 'lr_encrypted2');

  $count = $q->execute()->fetchField();

  $form['uc_libertyreserve_payment_completed']['#prefix'] = '<div class="uc_libertyreserve_infotext">';
  $form['uc_libertyreserve_payment_completed']['#suffix'] = '</div>';

  if ($count === 1) {
    $form['uc_libertyreserve_payment_completed']['#value'] = '<strong>' . t('Your payment was handled successfully.') . '</strong><br/>' . t('We will process your order immediately.');
  }
  else {
    $form['uc_libertyreserve_payment_completed']['#value'] = '<strong>' . t('Your payment is being processed by Liberty Reseve.') . '</strong><br/>' . t('We will process your order as soon as we receive the payment.');
  }

  $form['uc_libertyreserve_payment_completed']['#value'] .= '<br/>' . t('You can check the status of your order in your profile.') . '<br/>' . t('Feel free to continue shopping on our site.');
  $form['submit']['#type'] = 'submit';
  $form['submit']['#value'] = t('continue');
  $form['#action'] = url('cart');

  return $form;
}

/**
 *
 * @return type
 */
function uc_libertyreserve_payment_cancelled() {
  if(isset($_SESSION['cart_order'])){
    $order_id = intval($_SESSION['cart_order']);
    watchdog('uc_libertyreserve', 'Order with id %order_id failed or was cancelled', array('%order_id' => $order_id), WATCHDOG_WARNING);
  }

  $form['uc_libertyreserve_payment_cancelled'] = array(
    '#value' => '<strong>' . t('Your payment was cancelled.') . '</strong><br/>' . t('Feel free to continue shopping on our site.'),
    '#prefix' => '<div class="uc_libertyreserve_infotext">',
    '#suffix' => '</div>',
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('continue'),
  );
  $form['#action'] = url('cart');

  return $form;
}

/**
 * Function to receive the notification from libertyreserve about
 * handled payment
 */
function uc_libertyreserve_notification() {

// Do we have the corresponding order?
  $order_id = intval($_POST['order_id']);
  $order = uc_order_load($order_id);
  if ($order == FALSE) {
    watchdog('uc_libertyreserve', 'Notification attempt for non-existent order.', array(), WATCHDOG_ERROR);
    return;
  }

  // This way we don't get PHP e_notice errors
  if (!isset($_POST)) {
    $_POST = array();
  }

  // Do we have all Liberty Reserve POST data?
  $expected_post_data = array('lr_paidto', 'lr_paidby', 'lr_amnt', 'lr_fee_amnt', 'lr_currency', 'lr_transfer', 'lr_store', 'lr_timestamp', 'lr_encrypted', 'lr_encrypted2', 'order_id');
  $missing_post_data = FALSE;
  foreach ($expected_post_data as $post_data_key) {
    if (!isset($_POST[$post_data_key])) {
      $missing_post_data = TRUE;
      break;
    }
  }
  if ($missing_post_data) {
    watchdog('uc_libertyreserve', 'Notification attempt with missing post data, post data received was: %post_data.', array('%post_data' => var_export($_POST, TRUE)), WATCHDOG_ERROR);
    return;
  }

  // Is the payment valid
  $lr_encrypted2_expected = uc_libertyreserve_sha256($_POST['lr_paidto'], $_POST['lr_paidby'], $_POST['lr_store'], $_POST['lr_amnt'], $_POST['lr_transfer'], $_POST['lr_currency'], variable_get('uc_libertyreserve_security_word', ''));
  if ($lr_encrypted2_expected !== $_POST['lr_encrypted2']) {
    watchdog('uc_libertyreserve', 'Notification attempt with wrong hash string, post data received was: %post_data.', array('%post_data' => var_export($_POST, TRUE)), WATCHDOG_ERROR);
  }

  $record = array(
    'order_id' => $_POST['order_id'],
    'lr_paidto' => $_POST['lr_paidto'],
    'lr_paidby' => $_POST['lr_paidby'],
    'lr_amnt' => $_POST['lr_amnt'],
    'lr_fee_amnt' => $_POST['lr_fee_amnt'],
    'lr_currency' => $_POST['lr_currency'],
    'lr_transfer' => $_POST['lr_transfer'],
    'lr_store' => $_POST['lr_store'],
    'lr_timestamp' => $_POST['lr_timestamp'],
    'lr_merchant_ref' => $_POST['lr_merchant_ref'],
    'lr_encrypted' => $_POST['lr_encrypted'],
    'lr_encrypted2' => $_POST['lr_encrypted2'],
    'lr_encrypted2_expected' => $lr_encrypted2_expected
  );

  drupal_write_record("uc_libertyreserve_notification", $record);

  if ($_POST['lr_encrypted2'] === $lr_encrypted2_expected) { // processed
      $comment = t('Liberty Reserve transaction ID: @lr_transaction_id', array('@lr_transaction_id' => $_POST['lr_transfer']));
      uc_payment_enter($order_id, 'libertyreserve', $_POST['lr_encrypted2'], $order->uid, NULL, $comment);
      uc_cart_complete_sale($order);
      uc_order_comment_save($order_id, 0, t('Payment of @amount @currency submitted through Liberty Reserve.', array('@amount' => $_POST['lr_amnt'], '@currency' => $_POST['lr_currency'])), 'order', 'payment_received');
      uc_order_comment_save($order_id, 0, t('Liberty Reserve reported a payment of @amount @currency.', array('@amount' => $_POST['lr_amnt'], '@currency' => $_POST['lr_currency'])));
  }

}

/**
 * Function to generate the hash needed for payment verification
 */
function uc_libertyreserve_sha256($lr_paidto, $lr_paidby, $lr_store, $lr_amnt, $lr_transfer, $lr_currency, $lr_secret) {
  $lr_string = "$lr_paidto:$lr_paidby:$lr_store:$lr_amnt:$lr_transfer:$lr_currency:$lr_secret";
  return drupal_strtoupper(hash('sha256', $lr_string));
}
