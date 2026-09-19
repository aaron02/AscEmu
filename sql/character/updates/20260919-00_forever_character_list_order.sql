-- Forever / modern character-select ordering.
-- Positions are zero-based server-side. The official client account-data
-- format uses one-based positions.

CREATE TABLE IF NOT EXISTS `character_list_order` (
    `acct` INT UNSIGNED NOT NULL,
    `guid` BIGINT UNSIGNED NOT NULL,
    `listPosition` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`acct`, `guid`),
    KEY `idx_character_list_order_position` (`acct`, `listPosition`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
