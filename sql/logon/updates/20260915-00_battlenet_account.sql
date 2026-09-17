-- AscEmu Battle.net account split
--
-- Battle.net login identity and SRP credentials are account-level data and must
-- not live on a WoW game account. One Battle.net account can own multiple
-- rows from the existing `accounts` table.
--
-- This migration assumes the normal pre-Battle.net AscEmu account schema.
-- No temporary Battle.net credential columns on `accounts` are expected.

CREATE TABLE IF NOT EXISTS `battlenet_accounts` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `email` VARCHAR(320) NOT NULL,
    `srp_version` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `srp_salt` VARCHAR(64) NOT NULL DEFAULT '',
    `srp_verifier` VARCHAR(256) NOT NULL DEFAULT '',
    `battle_tag` VARCHAR(64) NOT NULL DEFAULT '',
    `country` CHAR(2) NOT NULL DEFAULT 'CH',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uq_battlenet_accounts_email` (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `battlenet_game_accounts` (
    `battlenet_account_id` INT UNSIGNED NOT NULL,
    `game_account_id` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`battlenet_account_id`, `game_account_id`),
    UNIQUE KEY `uq_battlenet_game_accounts_game_account` (`game_account_id`),
    KEY `idx_battlenet_game_accounts_bnet` (`battlenet_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Seed one Battle.net account per unique existing account email.
--
-- Existing AscEmu accounts do not contain Battle.net SRP credentials, so the
-- SRP fields intentionally keep their table defaults. They are populated by
-- the Battle.net account setup/password flow instead of being derived from the
-- legacy WoW account password data.
INSERT INTO `battlenet_accounts`
    (`email`, `battle_tag`, `country`)
SELECT
    a.`email`,
    CONCAT(SUBSTRING_INDEX(a.`email`, '@', 1), '#1'),
    'CH'
FROM `accounts` a
INNER JOIN (
    SELECT MIN(`id`) AS `source_id`
    FROM `accounts`
    WHERE `email` IS NOT NULL AND `email` <> ''
    GROUP BY UPPER(`email`)
) source ON source.`source_id` = a.`id`
ON DUPLICATE KEY UPDATE
    `email` = VALUES(`email`);

-- Link every existing WoW game account with an email to the corresponding
-- Battle.net account. Multiple WoW game accounts can therefore belong to the
-- same Battle.net login.
INSERT INTO `battlenet_game_accounts`
    (`battlenet_account_id`, `game_account_id`)
SELECT
    ba.`id`,
    a.`id`
FROM `accounts` a
INNER JOIN `battlenet_accounts` ba
    ON UPPER(ba.`email`) = UPPER(a.`email`)
WHERE a.`email` IS NOT NULL
  AND a.`email` <> ''
ON DUPLICATE KEY UPDATE
    `battlenet_account_id` = VALUES(`battlenet_account_id`);

-- `email` remains on `accounts` for the legacy AscEmu account system.
-- No Battle.net-specific columns are added to or removed from `accounts`.
