/* Standard small-business categories (loosely aligned with common
 * Schedule C / chart-of-accounts groupings). */
const DEFAULT_CATEGORIES = [
  // Income
  { name: 'Sales Revenue', type: 'income', icon: '🛒' },
  { name: 'Service Income', type: 'income', icon: '🧰' },
  { name: 'Consulting Fees', type: 'income', icon: '💼' },
  { name: 'Interest Income', type: 'income', icon: '🏦' },
  { name: 'Refunds & Rebates', type: 'income', icon: '↩️' },
  { name: 'Grants & Subsidies', type: 'income', icon: '🎗️' },
  { name: 'Other Income', type: 'income', icon: '➕' },

  // Expenses
  { name: 'Advertising & Marketing', type: 'expense', icon: '📣' },
  { name: 'Bank & Merchant Fees', type: 'expense', icon: '💳' },
  { name: 'Cost of Goods / Inventory', type: 'expense', icon: '📦' },
  { name: 'Contract Labor', type: 'expense', icon: '🤝' },
  { name: 'Equipment & Tools', type: 'expense', icon: '🛠️' },
  { name: 'Insurance', type: 'expense', icon: '🛡️' },
  { name: 'Legal & Professional Fees', type: 'expense', icon: '⚖️' },
  { name: 'Meals & Entertainment', type: 'expense', icon: '🍽️' },
  { name: 'Office Supplies', type: 'expense', icon: '📎' },
  { name: 'Payroll & Wages', type: 'expense', icon: '👥' },
  { name: 'Rent & Lease', type: 'expense', icon: '🏢' },
  { name: 'Repairs & Maintenance', type: 'expense', icon: '🔧' },
  { name: 'Shipping & Postage', type: 'expense', icon: '🚚' },
  { name: 'Software & Subscriptions', type: 'expense', icon: '💻' },
  { name: 'Taxes & Licenses', type: 'expense', icon: '🧾' },
  { name: 'Telephone & Internet', type: 'expense', icon: '📞' },
  { name: 'Training & Education', type: 'expense', icon: '🎓' },
  { name: 'Travel', type: 'expense', icon: '✈️' },
  { name: 'Utilities', type: 'expense', icon: '💡' },
  { name: 'Vehicle & Fuel', type: 'expense', icon: '⛽' },
  { name: 'Miscellaneous', type: 'expense', icon: '🗂️' },
];

const PAYMENT_METHODS = ['Cash', 'Debit card', 'Credit card', 'Bank transfer', 'Check', 'Mobile payment', 'Other'];

const CURRENCIES = [
  ['USD', 'US Dollar'], ['EUR', 'Euro'], ['GBP', 'British Pound'], ['CAD', 'Canadian Dollar'],
  ['AUD', 'Australian Dollar'], ['NZD', 'New Zealand Dollar'], ['INR', 'Indian Rupee'],
  ['LKR', 'Sri Lankan Rupee'], ['PKR', 'Pakistani Rupee'], ['BDT', 'Bangladeshi Taka'],
  ['JPY', 'Japanese Yen'], ['CNY', 'Chinese Yuan'], ['SGD', 'Singapore Dollar'],
  ['MYR', 'Malaysian Ringgit'], ['PHP', 'Philippine Peso'], ['AED', 'UAE Dirham'],
  ['SAR', 'Saudi Riyal'], ['ZAR', 'South African Rand'], ['NGN', 'Nigerian Naira'],
  ['KES', 'Kenyan Shilling'], ['MXN', 'Mexican Peso'], ['BRL', 'Brazilian Real'],
  ['CHF', 'Swiss Franc'], ['SEK', 'Swedish Krona'],
];

const ICON_CHOICES = ['🛒','🧰','💼','🏦','↩️','🎗️','➕','📣','💳','📦','🤝','🛠️','🛡️','⚖️','🍽️','📎','👥','🏢','🔧','🚚','💻','🧾','📞','🎓','✈️','💡','⛽','🗂️','🏷️','🎁','🧹','🖨️','📚','🏥','🅿️','🌐','🎨','📷','🧪','🌱'];

const DEFAULT_SETTINGS = {
  businessName: 'My Business',
  currency: 'USD',
  theme: 'system',
  compressReceipts: true,
  backupReminderDays: 7,
  lastBackupAt: null,
};
