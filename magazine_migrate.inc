<?php
/**
 * @file
 * Magazine migration classes.
 * 
 */


/**
 * Class holding common settings for article migration.
 *
 * @see migrate.drush.inc for migrate drush commands
 */
class MagazineBaseMigration extends Migration {

  /**
   *
   *
   */
  public function __parentConstruct() {
    parent::__construct();
  }

  public function __construct() {
    parent::__construct(MigrateGroup::getInstance('magazine_migrate'));

    $query = $this->getArticlesQuery();
    $this->source = new MigrateSourceSQL($query);

    $this->destination = new MigrateDestinationNode('page');

    // We instantiate the MigrateMap
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'id' => array(
          'type' => 'int',
          'not null' => TRUE,
          'description' => 'Article Id',
          'alias' => 'm',
        )
      ),
      MigrateDestinationNode::getKeySchema()
    );

    $this->addFieldMapping('title', 'headline')
      ->description(t('Mapping headline in source to field title'));
  }

  /**
  * Called before any mapping is done
  * @param unknown_type $row
  */
  public function prepareRow($row) {
    
  }

  /**
   * Sets various values before the node is inserted
   *
   */
  public function prepare(stdClass $node, stdClass $row) {

  }

  /**
   * Called after the node has been saved
   * @param unknown_type $entity
   * @param unknown_type $row
   */
  public function complete(stdClass $entity, stdClass $row) {
    
  }

  /**
   * 
   */
  public function getArticlesQuery(){
    $query = Database::getConnection('default', 'source')
      ->select('mag_articles', 'm')
      ->fields('m', array('id', 'headline'));
    return $query;
  }
}
